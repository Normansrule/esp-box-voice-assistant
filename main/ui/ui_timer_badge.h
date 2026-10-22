/*
 * ui_timer_badge.h — The countdown pill in the top-left corner.
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 *
 * A kitchen timer you cannot see is only half a kitchen timer: the whole point
 * is glancing over and knowing how long is left without having to ask. This is
 * that glance.
 *
 * It sits opposite the temperature pill, in the one corner nothing else uses —
 * the settings gear is bottom-left, the caption bubble bottom-right. It is
 * hidden entirely whenever no timer is running, so the buddy's face has the
 * screen to itself the rest of the time.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Build the badge. Starts hidden.
 *
 * @param parent  Screen or container to attach to.
 */
esp_err_t ui_timer_badge_create(lv_obj_t *parent);

/**
 * @brief Show the time remaining, or hide the badge.
 *
 * Safe to call from any task: it takes the display lock itself, which is what
 * lets the timer module call it straight from its one-second tick without
 * knowing anything about LVGL.
 *
 * @param seconds_left  Seconds to display.
 * @param running       False hides the badge.
 */
void ui_timer_badge_update(uint32_t seconds_left, bool running);

#ifdef __cplusplus
}
#endif
