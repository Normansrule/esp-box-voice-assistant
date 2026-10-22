/*
 * esp_timer.h — A controllable stand-in for ESP-IDF's timer, host builds only.
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 *
 * This one earns its keep rather than just silencing a dependency. The kitchen
 * timer in core/buddy_timer.c is the sort of code that is easy to get subtly
 * wrong — off by a second, or drifting over ten minutes — and impossible to
 * check by hand without sitting and waiting for it.
 *
 * So the clock here is a variable the tests move. A ten-minute countdown can be
 * verified in microseconds, and the tick callback can be fired on demand rather
 * than waited for.
 *
 * This file is NEVER part of the firmware build.
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

typedef void (*esp_timer_cb_t)(void *argument);

typedef enum {
    ESP_TIMER_TASK = 0,
    ESP_TIMER_ISR,
} esp_timer_dispatch_t;

typedef struct {
    esp_timer_cb_t       callback;
    void                *arg;
    esp_timer_dispatch_t dispatch_method;
    const char          *name;
    bool                 skip_unhandled_events;
} esp_timer_create_args_t;

typedef struct esp_timer_stub *esp_timer_handle_t;

/* --- The fake clock, and the one timer a test may have running ----------- */

extern int64_t         esp_timer_stub_now_us;
extern esp_timer_cb_t  esp_timer_stub_callback;
extern void           *esp_timer_stub_arg;
extern bool            esp_timer_stub_running;
extern int64_t         esp_timer_stub_period_us;

static inline int64_t esp_timer_get_time(void)
{
    return esp_timer_stub_now_us;
}

static inline esp_err_t esp_timer_create(const esp_timer_create_args_t *args,
                                         esp_timer_handle_t *out_handle)
{
    if (args == NULL || out_handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_timer_stub_callback = args->callback;
    esp_timer_stub_arg = args->arg;

    /* Any non-NULL value will do; nothing on the host dereferences it. */
    *out_handle = (esp_timer_handle_t)(void *)&esp_timer_stub_callback;
    return ESP_OK;
}

static inline esp_err_t esp_timer_start_periodic(esp_timer_handle_t handle, uint64_t period_us)
{
    (void)handle;
    esp_timer_stub_running = true;
    esp_timer_stub_period_us = (int64_t)period_us;
    return ESP_OK;
}

static inline esp_err_t esp_timer_stop(esp_timer_handle_t handle)
{
    (void)handle;
    esp_timer_stub_running = false;
    return ESP_OK;
}

/**
 * @brief Move the fake clock forward, firing ticks as a real timer would.
 *
 * @param microseconds  How far to advance.
 */
static inline void esp_timer_stub_advance(int64_t microseconds)
{
    const int64_t target = esp_timer_stub_now_us + microseconds;

    while (esp_timer_stub_running && esp_timer_stub_period_us > 0) {
        const int64_t next_tick = esp_timer_stub_now_us + esp_timer_stub_period_us;
        if (next_tick > target) {
            break;
        }
        esp_timer_stub_now_us = next_tick;
        if (esp_timer_stub_callback != NULL) {
            esp_timer_stub_callback(esp_timer_stub_arg);
        }
    }

    if (esp_timer_stub_now_us < target) {
        esp_timer_stub_now_us = target;
    }
}

/** @brief Put the fake clock and timer back to a known state. */
static inline void esp_timer_stub_reset(void)
{
    esp_timer_stub_now_us = 1000000;   /* Not zero, to catch "assumes t=0" bugs. */
    esp_timer_stub_running = false;
    esp_timer_stub_period_us = 0;
}
