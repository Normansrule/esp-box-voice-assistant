/*
 * buddy_motion.h — What the buddy does with its motion sensor and battery.
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 *
 *   lean the box      the eyes drift towards the low side
 *   shake it          dizzy face, "Whoa!"
 *   pick it up        surprised; put it back and it looks relieved
 *   battery low       says so once, with the measured voltage
 *
 * Plus a log line every BUDDY_BATTERY_REFRESH_MS:
 *   BATT,<ms>,<pin_mv>,<battery_mv>,<percent_est>
 *
 * STATUS: UNTESTED ON DEVICE.
 */

#pragma once

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Start the motion/battery task. Call after the face screen exists. */
esp_err_t buddy_motion_start(void);

/** Turn the eye-follows-lean behaviour on or off (on by default). */
void buddy_motion_set_gaze_enabled(bool enabled);

#ifdef __cplusplus
}
#endif
