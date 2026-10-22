/*
 * ui_sensor_readout.h — The temperature and humidity chip in the top right.
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 *
 * A small, quiet pill in the corner. It refreshes itself on a timer and hides
 * completely when no sensor dock is attached, so a bare box shows a clean face
 * rather than a row of dashes.
 */

#pragma once

#include <stdbool.h>
#include "esp_err.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Create the readout in the top-right corner of @p parent.
 *
 * @note Call with the display lock held.
 */
esp_err_t ui_sensor_readout_create(lv_obj_t *parent);

/**
 * @brief Read the sensor now and update the text.
 *
 * Called automatically on a timer; exposed so a settings change (Celsius to
 * Fahrenheit, say) can take effect immediately.
 */
void ui_sensor_readout_refresh(void);

/**
 * @brief Show or hide the readout.
 */
void ui_sensor_readout_set_visible(bool visible);

#ifdef __cplusplus
}
#endif
