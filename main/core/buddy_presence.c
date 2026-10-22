/*
 * buddy_presence.c — Noticing when somebody walks up to the desk.
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 *
 * See the header for why this exists. The tuning below is what stops a radar
 * from being irritating:
 *
 *   The desk has to have been EMPTY for a while before an arrival counts.
 *   Otherwise every shift in your chair reads as a new arrival, and the buddy
 *   looks up at you forty times an hour.
 *
 *   Presence has to be seen twice in a row to count. Microwave radar picks up
 *   the odd stray reflection, and a single frame of noise should not summon
 *   anything.
 */

#include "esp_log.h"
#include "esp_timer.h"

#include "buddy_presence.h"
#include "hw_sensors.h"

static const char *TAG = "presence";

/** How often the radar is read. */
#define POLL_PERIOD_US              (1000 * 1000)

/** Consecutive positive reads needed before believing somebody is there. */
#define CONFIRM_READS               2

/** How long the desk must have been empty for an arrival to be worth noting. */
#define EMPTY_BEFORE_ARRIVAL_US     ((int64_t)45 * 1000000)

static esp_timer_handle_t   s_poller;
static buddy_presence_cb_t  s_arrival_callback;
static bool                 s_someone_there;
static uint8_t              s_positive_reads;
static int64_t              s_empty_since_us;

static void poll_cb(void *argument)
{
    (void)argument;

    const bool detected = hw_sensors_is_someone_nearby();
    const int64_t now_us = esp_timer_get_time();

    if (!detected) {
        s_positive_reads = 0;
        if (s_someone_there) {
            s_someone_there = false;
            s_empty_since_us = now_us;
            ESP_LOGD(TAG, "Desk is empty");
        }
        return;
    }

    if (s_someone_there) {
        return;                     /* Already know. Nothing to announce. */
    }

    if (s_positive_reads < CONFIRM_READS) {
        s_positive_reads++;
        return;                     /* Could still be a stray reflection. */
    }

    s_someone_there = true;

    const bool was_empty_long_enough =
        (s_empty_since_us == 0) || ((now_us - s_empty_since_us) > EMPTY_BEFORE_ARRIVAL_US);

    if (!was_empty_long_enough) {
        ESP_LOGD(TAG, "Somebody back, but only just left");
        return;
    }

    ESP_LOGI(TAG, "Somebody arrived");

    if (s_arrival_callback != NULL) {
        s_arrival_callback();
    }
}

esp_err_t buddy_presence_start(buddy_presence_cb_t callback)
{
    if (!hw_sensors_is_dock_present()) {
        /*
         * No dock, no radar. Not an error — the buddy simply never notices
         * anyone, exactly as it did before this module existed.
         */
        ESP_LOGI(TAG, "No sensor dock, so nobody to notice");
        return ESP_ERR_NOT_FOUND;
    }

    s_arrival_callback = callback;
    s_empty_since_us = esp_timer_get_time();

    if (s_poller != NULL) {
        return ESP_OK;
    }

    const esp_timer_create_args_t arguments = {
        .callback = poll_cb,
        .name     = "presence",
        .dispatch_method = ESP_TIMER_TASK,
    };

    esp_err_t err = esp_timer_create(&arguments, &s_poller);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Could not create the poller: %s", esp_err_to_name(err));
        return err;
    }

    err = esp_timer_start_periodic(s_poller, POLL_PERIOD_US);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Could not start the poller: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "Watching for someone at the desk");
    return ESP_OK;
}

bool buddy_presence_is_someone_there(void)
{
    return s_someone_there;
}
