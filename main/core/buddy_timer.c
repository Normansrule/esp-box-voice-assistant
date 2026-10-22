/*
 * buddy_timer.c — A kitchen timer that needs no network.
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 *
 * See the header for what this is for. The parts worth knowing:
 *
 * The countdown is stored as a TARGET TIME rather than a decrementing counter.
 * A counter drifts — every tick that arrives a few milliseconds late is lost
 * forever, and over ten minutes that adds up to a timer that is visibly wrong.
 * Storing the finish line and subtracting from it means a late tick costs
 * nothing at all.
 *
 * esp_timer_get_time() is used rather than the wall clock, because the wall
 * clock can jump: the buddy sets its time from the network some seconds after
 * boot, and again whenever it reconnects. A timer set just before that jump
 * would finish early or late by however far the clock moved. The monotonic
 * microsecond counter has no such problem.
 */

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"

#include "buddy_timer.h"

static const char *TAG = "timer";

/** How often the badge on screen is refreshed. */
#define TICK_PERIOD_US              (1000 * 1000)

static esp_timer_handle_t        s_ticker;
static int64_t                   s_finish_us;
static bool                      s_running;
static buddy_timer_expired_cb_t  s_expired_callback;
static buddy_timer_tick_cb_t     s_tick_callback;

/**
 * @brief Seconds left, rounded UP.
 *
 * Rounding up is what makes a ten-minute timer show "10:00" for its first
 * second instead of flicking to "09:59" the instant it starts, which reads as
 * a bug even though it is not.
 */
static uint32_t seconds_left(void)
{
    if (!s_running) {
        return 0;
    }

    const int64_t remaining_us = s_finish_us - esp_timer_get_time();
    if (remaining_us <= 0) {
        return 0;
    }

    return (uint32_t)((remaining_us + 999999) / 1000000);
}

/**
 * @brief One second has passed.
 *
 * Runs on the esp_timer task, not in an interrupt, so calling into LVGL or the
 * assistant from the callbacks below is safe.
 */
static void ticker_cb(void *argument)
{
    (void)argument;

    if (!s_running) {
        return;
    }

    const uint32_t left = seconds_left();

    if (left > 0) {
        if (s_tick_callback != NULL) {
            s_tick_callback(left, true);
        }
        return;
    }

    /* Landed. Stop first, so anything the callbacks do sees a clean state. */
    s_running = false;
    esp_timer_stop(s_ticker);

    ESP_LOGI(TAG, "Timer finished");

    if (s_tick_callback != NULL) {
        s_tick_callback(0, false);
    }
    if (s_expired_callback != NULL) {
        s_expired_callback();
    }
}

esp_err_t buddy_timer_init(void)
{
    if (s_ticker != NULL) {
        return ESP_OK;
    }

    const esp_timer_create_args_t arguments = {
        .callback = ticker_cb,
        .name     = "buddy_timer",
        .dispatch_method = ESP_TIMER_TASK,
    };

    const esp_err_t err = esp_timer_create(&arguments, &s_ticker);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Could not create the timer: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "Kitchen timer ready");
    return ESP_OK;
}

void buddy_timer_set_expired_callback(buddy_timer_expired_cb_t callback)
{
    s_expired_callback = callback;
}

void buddy_timer_set_tick_callback(buddy_timer_tick_cb_t callback)
{
    s_tick_callback = callback;
}

esp_err_t buddy_timer_start(uint16_t minutes)
{
    if (s_ticker == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (minutes == 0 || minutes > BUDDY_TIMER_MAX_MINUTES) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_timer_stop(s_ticker);   /* Harmless when it is not running. */

    s_finish_us = esp_timer_get_time() + ((int64_t)minutes * 60 * 1000000);
    s_running = true;

    const esp_err_t err = esp_timer_start_periodic(s_ticker, TICK_PERIOD_US);
    if (err != ESP_OK) {
        s_running = false;
        ESP_LOGE(TAG, "Could not start the timer: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "Timer set for %u minute(s)", (unsigned)minutes);

    /* Paint the badge immediately rather than a second from now. */
    if (s_tick_callback != NULL) {
        s_tick_callback(seconds_left(), true);
    }

    return ESP_OK;
}

bool buddy_timer_cancel(void)
{
    if (!s_running) {
        return false;
    }

    s_running = false;
    esp_timer_stop(s_ticker);

    ESP_LOGI(TAG, "Timer cancelled");

    if (s_tick_callback != NULL) {
        s_tick_callback(0, false);
    }
    return true;
}

bool buddy_timer_is_running(void)
{
    return s_running;
}

uint32_t buddy_timer_seconds_remaining(void)
{
    return seconds_left();
}

void buddy_timer_format_clock(uint32_t seconds, char *destination, size_t size)
{
    if (destination == NULL || size == 0) {
        return;
    }

    const uint32_t minutes = seconds / 60;
    const uint32_t rest    = seconds % 60;

    snprintf(destination, size, "%lu:%02lu",
             (unsigned long)minutes, (unsigned long)rest);
}

bool buddy_timer_describe_remaining(char *destination, size_t size)
{
    if (destination == NULL || size == 0 || !s_running) {
        return false;
    }

    const uint32_t total   = seconds_left();
    const uint32_t minutes = total / 60;
    const uint32_t rest    = total % 60;

    /*
     * Said out loud as well as shown, so it is written the way a person would
     * say it. "1 minute" not "1 minutes", and no seconds at all when it is a
     * clean number of minutes.
     */
    if (minutes == 0) {
        snprintf(destination, size, "%lu second%s left",
                 (unsigned long)rest, (rest == 1) ? "" : "s");
    } else if (rest == 0) {
        snprintf(destination, size, "%lu minute%s left",
                 (unsigned long)minutes, (minutes == 1) ? "" : "s");
    } else {
        snprintf(destination, size, "%lu minute%s and %lu second%s left",
                 (unsigned long)minutes, (minutes == 1) ? "" : "s",
                 (unsigned long)rest, (rest == 1) ? "" : "s");
    }

    return true;
}

bool buddy_timer_minutes_from_phrase(const char *phrase, uint16_t *minutes)
{
    if (phrase == NULL || minutes == NULL) {
        return false;
    }

    /*
     * Matched against the phrases registered in voice_commands.c, longest
     * first — "twenty" has to be tested before "two" would be, or "set a timer
     * for twenty minutes" quietly becomes a two-minute timer.
     */
    static const struct {
        const char *word;
        uint16_t    minutes;
    } spellings[] = {
        { "thirty", 30 },
        { "twenty", 20 },
        { "ten",    10 },
        { "five",    5 },
        { "two",     2 },
        { "one",     1 },
    };

    for (size_t i = 0; i < sizeof(spellings) / sizeof(spellings[0]); i++) {
        if (strstr(phrase, spellings[i].word) != NULL) {
            *minutes = spellings[i].minutes;
            return true;
        }
    }

    return false;
}
