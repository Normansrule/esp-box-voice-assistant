/*
 * hw_display.h — The LCD panel, the touch screen, and the LVGL lock.
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 *
 * LVGL is not thread-safe. Everything that touches a widget must hold one
 * mutex, and this module owns it. Any task that is not LVGL's own rendering
 * task must wrap its drawing like this:
 *
 *      if (hw_display_lock(0)) {
 *          ... call LVGL functions ...
 *          hw_display_unlock();
 *      }
 *
 * Forgetting the lock produces crashes that look random and are miserable to
 * debug, so every module in this project that draws goes through here.
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
 * @brief Bring up the panel, the touch controller and the LVGL port task.
 *
 * The backlight stays off until hw_display_set_backlight_on() is called, so the
 * user never sees a half-drawn boot screen.
 */
esp_err_t hw_display_init(void);

/**
 * @brief Take the LVGL mutex.
 *
 * @param timeout_ms  How long to wait. 0 means wait forever.
 * @return true when the lock was taken; false on timeout (do not draw).
 */
bool hw_display_lock(uint32_t timeout_ms);

/**
 * @brief Release the LVGL mutex taken by hw_display_lock().
 */
void hw_display_unlock(void);

/**
 * @brief Turn the backlight on at the brightness stored in the settings.
 */
void hw_display_set_backlight_on(void);

/**
 * @brief Turn the backlight completely off.
 */
void hw_display_set_backlight_off(void);

/**
 * @brief Set the backlight level.
 *
 * @param percent  10-100. Values below 10 are clamped so the screen never
 *                 looks broken to the user.
 */
void hw_display_set_brightness(uint8_t percent);

#ifdef __cplusplus
}
#endif
