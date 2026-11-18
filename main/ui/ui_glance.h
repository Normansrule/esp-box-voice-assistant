/*
 * ui_glance.h — The glance page: the whole screen as a desk clock.
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 *
 * STATUS: UNTESTED ON DEVICE. Its text rules (ui_glance_logic.c) are VERIFIED
 * IN TEST.
 *
 * Swipe the face left or right (or type `glance` in the serial monitor) and
 * the screen shows, at a size readable from across the room:
 *
 *   the date            once the clock has synced over Wi-Fi
 *   the time            60 px digits, "--:--" until it has
 *   room temp, humidity and battery (est.) tiles, "--" with a reason when a
 *   reading is missing
 *
 * A tap or another swipe goes back to the face, and so does hearing the wake
 * word, so the buddy is always visible when it is listening. After
 * UI_GLANCE_TIMEOUT_MS on its own it returns to the face too.
 */

#pragma once

#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define UI_GLANCE_TIMEOUT_MS    20000

/** Open the glance page. Call with the display lock held. Does nothing if open. */
void ui_glance_open(void);

/** Close it. Call with the display lock held. Does nothing if closed. */
void ui_glance_close(void);

/** True while the glance page covers the face. Call with the display lock held. */
bool ui_glance_is_open(void);

#ifdef __cplusplus
}
#endif
