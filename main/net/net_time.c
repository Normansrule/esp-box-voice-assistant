/*
 * net_time.c — Network time and time zones.
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */

#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "esp_log.h"
#include "esp_netif_sntp.h"
#include "esp_sntp.h"

#include "buddy_settings.h"
#include "net_time.h"

static const char *TAG = "net_time";

/** Any year before this means the clock has not been set yet. */
#define FIRST_BELIEVABLE_YEAR   2023

static bool s_started;

/**
 * @brief Called by the SNTP client each time it lands a fresh timestamp.
 */
static void time_synchronised_cb(struct timeval *received)
{
    (void)received;

    time_t now = time(NULL);
    struct tm local;
    localtime_r(&now, &local);

    char formatted[64];
    strftime(formatted, sizeof(formatted), "%Y-%m-%d %H:%M:%S", &local);
    ESP_LOGI(TAG, "Clock set: %s", formatted);
}

void net_time_set_timezone(const char *posix_timezone)
{
    const char *timezone = posix_timezone;

    if (timezone == NULL || timezone[0] == '\0') {
        timezone = buddy_settings_get()->timezone;
    }
    if (timezone == NULL || timezone[0] == '\0') {
        timezone = "UTC0";
    }

    /*
     * setenv + tzset is how the C library learns about a time zone. Everything
     * that calls localtime() afterwards, including strftime, picks it up.
     */
    setenv("TZ", timezone, 1);
    tzset();

    ESP_LOGI(TAG, "Time zone set to %s", timezone);
}

/** Public time servers, tried in order. */
#define PRIMARY_TIME_SERVER     "pool.ntp.org"
#define SECONDARY_TIME_SERVER   "time.google.com"

esp_err_t net_time_start(void)
{
    if (s_started) {
        return ESP_OK;
    }

    net_time_set_timezone(NULL);

    /*
     * ONE server in the config, deliberately.
     *
     * This used to ask for two servers plus whatever the router advertised over
     * DHCP, and it never worked. Taking a time server from DHCP needs
     * CONFIG_LWIP_DHCP_GET_NTP_SRV, which is off by default; asking for it
     * anyway makes esp_netif_sntp_init() reject the WHOLE config:
     *
     *     E esp_netif_sntp: Tried to configure SNTP server from DHCP, while
     *                       disabled. Please enable CONFIG_LWIP_DHCP_GET_NTP_SRV
     *     E net_time: Could not start the time client: ESP_ERR_INVALID_ARG
     *
     * So the time client never started at all, the clock stayed at 1970, and
     * every HTTPS request to the cloud failed its certificate check — because a
     * certificate issued in 2026 is "not yet valid" to a box that thinks it is
     * still 1970. The buddy was on Wi-Fi, had a valid key, and could not answer
     * a single question.
     *
     * The multi-server form also depended on CONFIG_LWIP_SNTP_MAX_SERVERS being
     * raised from its default of 1. Two build options, both silently required,
     * both easy to lose when an sdkconfig is regenerated. The single-server
     * form needs neither, so it cannot break the same way twice; the fallback
     * server is added afterwards, and only if there is room for it.
     */
    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG(PRIMARY_TIME_SERVER);
    config.start = true;
    config.sync_cb = time_synchronised_cb;

    const esp_err_t err = esp_netif_sntp_init(&config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Could not start the time client: %s", esp_err_to_name(err));
        ESP_LOGE(TAG, "Without a clock, nothing over HTTPS can work.");
        return err;
    }

    s_started = true;

    /*
     * A spare, when the build has a slot for one. Guarded rather than assumed:
     * with a single slot, setting index 1 writes past the end of lwIP's server
     * table.
     */
#if defined(CONFIG_LWIP_SNTP_MAX_SERVERS) && (CONFIG_LWIP_SNTP_MAX_SERVERS > 1)
    esp_sntp_setservername(1, SECONDARY_TIME_SERVER);
    ESP_LOGI(TAG, "Asking %s what time it is (%s in reserve)",
             PRIMARY_TIME_SERVER, SECONDARY_TIME_SERVER);
#else
    ESP_LOGI(TAG, "Asking %s what time it is", PRIMARY_TIME_SERVER);
#endif

    return ESP_OK;
}

bool net_time_is_synchronised(void)
{
    time_t now = time(NULL);
    struct tm local;
    localtime_r(&now, &local);
    return (local.tm_year + 1900) >= FIRST_BELIEVABLE_YEAR;
}
