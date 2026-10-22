/*
 * net_wifi.c — Station-mode Wi-Fi with retries and a scanner.
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 *
 * THREE THINGS THIS FILE GETS RIGHT THAT THE OBVIOUS VERSION DOES NOT
 * -------------------------------------------------------------------
 *
 * 1. NOTHING HERE BLOCKS. Every one of these handlers runs on the system event
 *    task, which is the same task the Wi-Fi driver and the DHCP client deliver
 *    their events on. A vTaskDelay() in a disconnect handler — the natural way
 *    to write "wait a second, then retry" — stops the address from ever being
 *    delivered while it sleeps, and backs events up behind it. Retries are
 *    scheduled on a timer instead.
 *
 * 2. A DELIBERATE DISCONNECT IS NOT A FAILURE. Switching networks means tearing
 *    down the old association first, and esp_wifi_disconnect() is asynchronous:
 *    the event it produces lands in the same handler that counts failures. Left
 *    unhandled, every connection attempt spends one of its own retries on the
 *    teardown it asked for, and races a second esp_wifi_connect() against the
 *    first. s_connect_after_teardown is what keeps those two apart.
 *
 * 3. ASSOCIATING AND GETTING AN ADDRESS ARE DIFFERENT STEPS, AND EITHER CAN
 *    STALL. A router that accepts the password but never hands out a lease
 *    produces NO disconnect event at all — so a state machine that only counts
 *    disconnects waits forever, showing "Connecting..." with nothing wrong that
 *    it can see. There is a deadline on the whole business now, and the failure
 *    it reports says which half went wrong, because the fixes are completely
 *    different.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"

#include "buddy_config.h"
#include "buddy_settings.h"
#include "net_wifi.h"

static const char *TAG = "net_wifi";

/** How many networks a single scan will report back. */
#define SCAN_MAX_RESULTS        16

/** Gap before the first retry. Later ones back off from here. */
#define RETRY_BASE_DELAY_MS     1000

/** Longest gap between retries, however many have failed. */
#define RETRY_MAX_DELAY_MS      5000

static net_wifi_state_t    s_state = NET_WIFI_IDLE;
static net_wifi_state_cb_t s_state_callback;
static net_wifi_scan_cb_t  s_scan_callback;

static char s_ip_address[16];
static char s_error_text[80];
static int  s_retries_left;
static bool s_initialised;

/** True while we want to be on the network. Cleared by disconnect() and failure. */
static bool s_want_connection;

/** True between STA_CONNECTED and STA_DISCONNECTED: the radio link is up. */
static bool s_associated;

/** True once an address has ever been obtained on this power cycle. */
static bool s_ever_connected;

/**
 * Set when we tear down an association purely so we can start a different one.
 * The disconnect event that follows is ours and must not be counted as a
 * failure — see note 2 at the top of the file.
 */
static bool s_connect_after_teardown;

static esp_timer_handle_t s_retry_timer;
static esp_timer_handle_t s_deadline_timer;

/* ------------------------------------------------------------------------ */
/* State changes                                                             */
/* ------------------------------------------------------------------------ */

static void set_state(net_wifi_state_t state)
{
    if (s_state == state) {
        return;
    }
    s_state = state;

    if (s_state_callback != NULL) {
        s_state_callback(state);
    }
}

/* ------------------------------------------------------------------------ */
/* Timers                                                                    */
/* ------------------------------------------------------------------------ */

static void stop_timers(void)
{
    if (s_retry_timer != NULL) {
        esp_timer_stop(s_retry_timer);
    }
    if (s_deadline_timer != NULL) {
        esp_timer_stop(s_deadline_timer);
    }
}

/**
 * @brief Schedule the next attempt, backing off a little each time.
 */
static void schedule_retry(void)
{
    if (s_retry_timer == NULL) {
        return;
    }

    const int attempt = BUDDY_WIFI_MAX_RETRIES - s_retries_left;
    int delay_ms = RETRY_BASE_DELAY_MS * (attempt + 1);
    if (delay_ms > RETRY_MAX_DELAY_MS) {
        delay_ms = RETRY_MAX_DELAY_MS;
    }

    esp_timer_stop(s_retry_timer);
    esp_timer_start_once(s_retry_timer, (uint64_t)delay_ms * 1000);
}

static void retry_timer_cb(void *argument)
{
    (void)argument;

    if (!s_want_connection) {
        return;
    }

    ESP_LOGI(TAG, "Retrying");
    esp_wifi_connect();
}

/**
 * @brief The whole attempt has taken too long.
 *
 * This is the case the old code could not see. If the radio associated but no
 * address arrived, there is no disconnect event to count and nothing ever times
 * out — the screen just says "Connecting..." indefinitely.
 */
static void deadline_timer_cb(void *argument)
{
    (void)argument;

    if (s_state != NET_WIFI_CONNECTING || !s_want_connection) {
        return;
    }

    if (s_associated) {
        /*
         * The password was accepted — association succeeded — and then the
         * router never completed DHCP. Saying "wrong password" here would send
         * somebody off to re-type a password that was right all along.
         */
        strncpy(s_error_text,
                "Joined, but no IP address. Check the router's DHCP.",
                sizeof(s_error_text) - 1);
    } else {
        strncpy(s_error_text,
                "No answer from the network. Is it 2.4 GHz?",
                sizeof(s_error_text) - 1);
    }
    s_error_text[sizeof(s_error_text) - 1] = '\0';

    if (s_ever_connected) {
        /*
         * We have been on this network before, so this is a router reboot or a
         * walk out of range rather than a mistake worth reporting. Keep trying
         * quietly and give it another window.
         */
        ESP_LOGW(TAG, "Still trying: %s", s_error_text);
        esp_timer_start_once(s_deadline_timer,
                             (uint64_t)BUDDY_WIFI_CONNECT_TIMEOUT_S * 1000000);
        return;
    }

    ESP_LOGE(TAG, "Timed out: %s", s_error_text);

    s_want_connection = false;
    stop_timers();
    esp_wifi_disconnect();
    set_state(NET_WIFI_FAILED);
}

static void arm_deadline(void)
{
    if (s_deadline_timer == NULL) {
        return;
    }
    esp_timer_stop(s_deadline_timer);
    esp_timer_start_once(s_deadline_timer,
                         (uint64_t)BUDDY_WIFI_CONNECT_TIMEOUT_S * 1000000);
}

/* ------------------------------------------------------------------------ */
/* Wi-Fi and IP events                                                       */
/* ------------------------------------------------------------------------ */

/**
 * @brief Turn a disconnect reason code into something worth reading.
 *
 * The numeric code is always logged as well. These are the ones that actually
 * come up on a desk; anything else falls through to the number, which is more
 * use than a vague apology.
 */
static void describe_disconnect(int reason)
{
    switch (reason) {
    case WIFI_REASON_AUTH_FAIL:
    case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
    case WIFI_REASON_HANDSHAKE_TIMEOUT:
        strncpy(s_error_text, "Wrong password", sizeof(s_error_text) - 1);
        break;

    case WIFI_REASON_NO_AP_FOUND:
        /*
         * By far the most common cause on this chip: the ESP32-S3 has no 5 GHz
         * radio at all, so a network that only advertises there is invisible to
         * it no matter how strong the signal is on your phone.
         */
        strncpy(s_error_text, "Network not found. Is it 2.4 GHz?",
                sizeof(s_error_text) - 1);
        break;

    case WIFI_REASON_ASSOC_EXPIRE:
    case WIFI_REASON_ASSOC_TOOMANY:
        strncpy(s_error_text, "The router turned me away. It may be full.",
                sizeof(s_error_text) - 1);
        break;

    case WIFI_REASON_BEACON_TIMEOUT:
    case WIFI_REASON_AUTH_EXPIRE:
        strncpy(s_error_text, "Signal lost. Try moving me closer.",
                sizeof(s_error_text) - 1);
        break;

    default:
        snprintf(s_error_text, sizeof(s_error_text),
                 "Could not connect (reason %d)", reason);
        break;
    }
    s_error_text[sizeof(s_error_text) - 1] = '\0';
}

static void handle_disconnected(const wifi_event_sta_disconnected_t *info)
{
    const int reason = (info != NULL) ? info->reason : 0;

    s_associated = false;
    s_ip_address[0] = '\0';

    /*
     * A disconnect we asked for, so that a new association could start. Not a
     * failure, and it must not spend one of the attempts.
     */
    if (s_connect_after_teardown) {
        s_connect_after_teardown = false;
        ESP_LOGD(TAG, "Old association dropped; connecting to the new one");
        esp_wifi_connect();
        return;
    }

    if (!s_want_connection) {
        stop_timers();
        set_state(NET_WIFI_IDLE);
        return;
    }

    if (s_retries_left > 0) {
        s_retries_left--;
        ESP_LOGW(TAG, "Disconnected (reason %d); %d attempt(s) left",
                 reason, s_retries_left);
        schedule_retry();
        set_state(NET_WIFI_CONNECTING);
        return;
    }

    /*
     * Out of attempts. If this network has worked before, keep going quietly —
     * a router reboot should not leave the buddy permanently offline until
     * somebody notices and re-enters a password that was never wrong.
     */
    if (s_ever_connected) {
        s_retries_left = BUDDY_WIFI_MAX_RETRIES;
        ESP_LOGW(TAG, "Disconnected (reason %d); still trying", reason);
        schedule_retry();
        set_state(NET_WIFI_CONNECTING);
        return;
    }

    describe_disconnect(reason);
    ESP_LOGE(TAG, "Giving up (reason %d): %s", reason, s_error_text);

    s_want_connection = false;
    stop_timers();
    set_state(NET_WIFI_FAILED);
}

static void wifi_event_handler(void *argument, esp_event_base_t base,
                               int32_t event_id, void *event_data)
{
    (void)argument;

    if (base == WIFI_EVENT) {
        switch (event_id) {
        case WIFI_EVENT_STA_START:
            if (s_want_connection) {
                esp_wifi_connect();
            }
            return;

        case WIFI_EVENT_STA_CONNECTED: {
            /*
             * Radio link up, password accepted. NOT online yet — DHCP has still
             * to run, and it is entirely possible for it never to finish.
             * Recording this separately is what lets the deadline below report
             * which of the two steps actually stalled.
             */
            const wifi_event_sta_connected_t *info =
                (const wifi_event_sta_connected_t *)event_data;

            s_associated = true;
            ESP_LOGI(TAG, "Associated with \"%.*s\" on channel %d, waiting for an address",
                     (info != NULL) ? info->ssid_len : 0,
                     (info != NULL) ? (const char *)info->ssid : "",
                     (info != NULL) ? info->channel : 0);
            return;
        }

        case WIFI_EVENT_STA_DISCONNECTED:
            handle_disconnected((const wifi_event_sta_disconnected_t *)event_data);
            return;

        default:
            return;
        }
    }

    if (base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *event = (const ip_event_got_ip_t *)event_data;

        snprintf(s_ip_address, sizeof(s_ip_address), IPSTR, IP2STR(&event->ip_info.ip));
        s_error_text[0] = '\0';
        s_retries_left = BUDDY_WIFI_MAX_RETRIES;
        s_ever_connected = true;

        stop_timers();

        ESP_LOGI(TAG, "Connected, address %s", s_ip_address);
        set_state(NET_WIFI_CONNECTED);
        return;
    }
}

/* ------------------------------------------------------------------------ */
/* Set-up                                                                    */
/* ------------------------------------------------------------------------ */

esp_err_t net_wifi_init(void)
{
    if (s_initialised) {
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "netif init failed");

    /*
     * The default event loop may already exist — the speech and audio stacks
     * create it too — so "already invoked" is a success, not a failure.
     */
    esp_err_t err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "Event loop failed: %s", esp_err_to_name(err));
        return err;
    }

    esp_netif_create_default_wifi_sta();

    wifi_init_config_t init_config = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&init_config), TAG, "wifi init failed");

    ESP_RETURN_ON_ERROR(esp_event_handler_instance_register(
                            WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL, NULL),
                        TAG, "wifi handler registration failed");
    ESP_RETURN_ON_ERROR(esp_event_handler_instance_register(
                            IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event_handler, NULL, NULL),
                        TAG, "ip handler registration failed");

    /* Retries and the overall deadline both run here rather than in a handler. */
    const esp_timer_create_args_t retry_args = {
        .callback = retry_timer_cb,
        .name = "wifi_retry",
        .dispatch_method = ESP_TIMER_TASK,
    };
    ESP_RETURN_ON_ERROR(esp_timer_create(&retry_args, &s_retry_timer),
                        TAG, "retry timer failed");

    const esp_timer_create_args_t deadline_args = {
        .callback = deadline_timer_cb,
        .name = "wifi_deadline",
        .dispatch_method = ESP_TIMER_TASK,
    };
    ESP_RETURN_ON_ERROR(esp_timer_create(&deadline_args, &s_deadline_timer),
                        TAG, "deadline timer failed");

    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "sta mode failed");
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "wifi start failed");

    /*
     * Modem sleep between beacons. The radio is idle most of the time on a desk
     * toy, and this keeps the box noticeably cooler.
     */
    esp_wifi_set_ps(WIFI_PS_MIN_MODEM);

    s_initialised = true;
    ESP_LOGI(TAG, "Wi-Fi ready");
    return ESP_OK;
}

/* ------------------------------------------------------------------------ */
/* Connecting                                                                */
/* ------------------------------------------------------------------------ */

/**
 * @brief Push an SSID and password into the driver and start connecting.
 */
static esp_err_t start_connection(const char *ssid, const char *password)
{
    wifi_config_t config = { 0 };

    strncpy((char *)config.sta.ssid, ssid, sizeof(config.sta.ssid) - 1);
    if (password != NULL) {
        strncpy((char *)config.sta.password, password, sizeof(config.sta.password) - 1);
    }

    /*
     * Accept anything from an open network upwards. Setting a floor here is a
     * classic way to make a perfectly good network unjoinable: a threshold of
     * WPA2 quietly refuses the WPA/WPA2-mixed mode a lot of routers still ship
     * in, and reports it as an ordinary failure.
     */
    config.sta.threshold.authmode = WIFI_AUTH_OPEN;

    /* Offered, not demanded — required PMF locks out most older routers. */
    config.sta.pmf_cfg.capable = true;
    config.sta.pmf_cfg.required = false;

    /*
     * Look at every channel rather than stopping at the first hit. Fast scan
     * takes whichever matching AP answers first, which on a mesh or a repeated
     * network can be the one with the worst signal in the house.
     */
    config.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
    config.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;

    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &config), TAG, "set config failed");

    s_want_connection = true;
    s_associated = false;
    s_retries_left = BUDDY_WIFI_MAX_RETRIES;
    s_error_text[0] = '\0';
    s_ip_address[0] = '\0';

    stop_timers();
    set_state(NET_WIFI_CONNECTING);
    arm_deadline();

    ESP_LOGI(TAG, "Connecting to \"%s\" (%s)", ssid,
             (password != NULL && password[0] != '\0') ? "with a password" : "open");

    /*
     * If there is an association to drop, drop it and connect when the
     * disconnect event arrives — not immediately afterwards. Calling connect
     * while a disconnect is still in flight starts two attempts racing, and the
     * loser reports a failure that looks exactly like a wrong password.
     */
    wifi_ap_record_t current;
    if (esp_wifi_sta_get_ap_info(&current) == ESP_OK) {
        s_connect_after_teardown = true;
        return esp_wifi_disconnect();
    }

    return esp_wifi_connect();
}

esp_err_t net_wifi_connect_using_saved_credentials(void)
{
    ESP_RETURN_ON_FALSE(s_initialised, ESP_ERR_INVALID_STATE, TAG, "not initialised");

    const buddy_settings_t *settings = buddy_settings_get();
    if (settings->wifi_ssid[0] == '\0') {
        ESP_LOGI(TAG, "No network configured yet");
        strncpy(s_error_text, "No network configured", sizeof(s_error_text) - 1);
        set_state(NET_WIFI_IDLE);
        return ESP_ERR_INVALID_STATE;
    }

    return start_connection(settings->wifi_ssid, settings->wifi_password);
}

esp_err_t net_wifi_connect(const char *ssid, const char *password)
{
    ESP_RETURN_ON_FALSE(s_initialised, ESP_ERR_INVALID_STATE, TAG, "not initialised");
    ESP_RETURN_ON_FALSE(ssid != NULL && ssid[0] != '\0', ESP_ERR_INVALID_ARG, TAG, "no ssid");

    /*
     * A fresh attempt, so forget that we were ever on a network. Otherwise the
     * "keep trying quietly" path above would swallow a genuinely wrong password
     * typed for a DIFFERENT network and never report anything.
     */
    s_ever_connected = false;

    /* Remember them so the buddy reconnects by itself after a power cut. */
    buddy_settings_t *settings = buddy_settings_get();
    strncpy(settings->wifi_ssid, ssid, sizeof(settings->wifi_ssid) - 1);
    settings->wifi_ssid[sizeof(settings->wifi_ssid) - 1] = '\0';

    memset(settings->wifi_password, 0, sizeof(settings->wifi_password));
    if (password != NULL) {
        strncpy(settings->wifi_password, password, sizeof(settings->wifi_password) - 1);
    }
    buddy_settings_save();

    return start_connection(settings->wifi_ssid, settings->wifi_password);
}

void net_wifi_disconnect(void)
{
    s_want_connection = false;
    s_connect_after_teardown = false;
    stop_timers();
    esp_wifi_disconnect();
    s_ip_address[0] = '\0';
    set_state(NET_WIFI_IDLE);
}

/* ------------------------------------------------------------------------ */
/* Scanning                                                                  */
/* ------------------------------------------------------------------------ */

/**
 * @brief Runs one blocking scan, reports the results, then disappears.
 */
static void scan_task(void *argument)
{
    (void)argument;

    net_wifi_network_t *results = calloc(SCAN_MAX_RESULTS, sizeof(net_wifi_network_t));
    uint16_t found = 0;

    if (results != NULL) {
        const wifi_scan_config_t scan_config = {
            .show_hidden = false,
            .scan_type = WIFI_SCAN_TYPE_ACTIVE,
        };

        if (esp_wifi_scan_start(&scan_config, true /* block until done */) == ESP_OK) {
            wifi_ap_record_t *records = calloc(SCAN_MAX_RESULTS, sizeof(wifi_ap_record_t));
            if (records != NULL) {
                uint16_t wanted = SCAN_MAX_RESULTS;
                if (esp_wifi_scan_get_ap_records(&wanted, records) == ESP_OK) {
                    for (uint16_t i = 0; i < wanted && found < SCAN_MAX_RESULTS; i++) {
                        if (records[i].ssid[0] == '\0') {
                            continue;   /* Hidden network, nothing to show. */
                        }
                        strncpy(results[found].ssid, (const char *)records[i].ssid,
                                sizeof(results[found].ssid) - 1);
                        results[found].signal_strength = records[i].rssi;
                        results[found].needs_password = (records[i].authmode != WIFI_AUTH_OPEN);
                        found++;
                    }
                }
                free(records);
            }
        } else {
            ESP_LOGW(TAG, "Scan could not be started");
        }
    }

    /*
     * Everything a scan can see is 2.4 GHz, because that is the only band this
     * chip has. Worth saying in the log: a network missing from this list is
     * almost always on 5 GHz rather than out of range.
     */
    ESP_LOGI(TAG, "Scan found %u networks (2.4 GHz only — this chip has no 5 GHz radio)",
             found);

    if (s_scan_callback != NULL) {
        s_scan_callback(results, found);
    }

    free(results);
    s_scan_callback = NULL;
    vTaskDelete(NULL);
}

esp_err_t net_wifi_scan_async(net_wifi_scan_cb_t callback)
{
    ESP_RETURN_ON_FALSE(s_initialised, ESP_ERR_INVALID_STATE, TAG, "not initialised");
    ESP_RETURN_ON_FALSE(callback != NULL, ESP_ERR_INVALID_ARG, TAG, "callback required");
    ESP_RETURN_ON_FALSE(s_scan_callback == NULL, ESP_ERR_INVALID_STATE, TAG, "scan already running");

    s_scan_callback = callback;

    const BaseType_t created = xTaskCreate(scan_task, "wifi_scan", 4 * 1024, NULL, 4, NULL);
    if (created != pdPASS) {
        s_scan_callback = NULL;
        return ESP_FAIL;
    }
    return ESP_OK;
}

/* ------------------------------------------------------------------------ */
/* Queries                                                                   */
/* ------------------------------------------------------------------------ */

bool net_wifi_is_connected(void)
{
    return s_state == NET_WIFI_CONNECTED;
}

net_wifi_state_t net_wifi_get_state(void)
{
    return s_state;
}

const char *net_wifi_get_ip_address(void)
{
    return s_ip_address;
}

const char *net_wifi_get_ssid(void)
{
    return buddy_settings_get()->wifi_ssid;
}

const char *net_wifi_get_error(void)
{
    return s_error_text;
}

bool net_wifi_is_associated(void)
{
    return s_associated;
}

void net_wifi_set_state_callback(net_wifi_state_cb_t callback)
{
    s_state_callback = callback;
}
