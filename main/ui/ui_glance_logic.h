/*
 * ui_glance_logic.h — The text on the glance page, as pure functions.
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 *
 * STATUS: VERIFIED IN TEST (host tests). UNTESTED ON DEVICE.
 *
 * Kept free of LVGL so the host tests can check the honesty rules: a missing
 * reading shows "--" with a caption that says why, never a made-up number,
 * and the battery figure always carries "~" because it is an estimate.
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** One tile: a big value (digits font) and a small caption under it. */
typedef struct {
    char value[12];      /*!< Digits, "%", "~", "°", "C", "F", "." and "-" only */
    char caption[24];    /*!< Any text, drawn at 12 px                        */
} ui_glance_tile_t;

/** Room temperature. @p valid false (no dock, no reading yet) gives "--". */
void ui_glance_temperature(bool valid, float celsius, bool fahrenheit, ui_glance_tile_t *out);

/** Relative humidity. @p valid false gives "--". Clamped to 0..100. */
void ui_glance_humidity(bool valid, float percent, ui_glance_tile_t *out);

/**
 * Battery. Not available (no dock/ADC) and no cell both give "--", with a
 * caption that says which. Otherwise "~NN%" and "Battery est.", or
 * "Battery low" when @p low.
 */
void ui_glance_battery(bool available, bool cell_detected, uint8_t percent_est,
                       bool low, ui_glance_tile_t *out);

#ifdef __cplusplus
}
#endif
