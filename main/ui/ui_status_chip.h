/*
 * ui_status_chip.h — The live status chip in the top-left corner.
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 *
 * STATUS: UNTESTED ON DEVICE. Its decisions (ui_status_logic.c) are VERIFIED
 * IN TEST.
 *
 * One small chip that says what the box is really doing, read from the same
 * state the rest of the firmware acts on:
 *
 *   Mic off      the mute switch is on (hw_buttons_is_muted)
 *   ||||| meter  listening, bars from the live microphone peak
 *   Online       a request to the home or cloud brain is out
 *   3:07         idle, once the clock has synced over Wi-Fi
 *
 * It sits next to the timer pill when a timer runs, and in the corner when
 * not, so the two never overlap and the top-right stays the sensor chip's.
 */

#pragma once

#include "esp_err.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Build the chip and start its poll timer. Call with the display lock held.
 */
esp_err_t ui_status_chip_create(lv_obj_t *parent);

#ifdef __cplusplus
}
#endif
