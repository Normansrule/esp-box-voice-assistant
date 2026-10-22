/*
 * hw_buttons.h — The physical buttons on the box.
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 *
 * The ESP32-S3-BOX-3 has a mute switch on the top edge and a boot button on the
 * back. The mute switch is wired straight through to the microphone here, so
 * flipping it silences the buddy's ears no matter what else is going on.
 */

#pragma once

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Called when the mute switch changes position.
 *
 * @param muted  true when the switch is in the muted position.
 */
typedef void (*hw_buttons_mute_changed_cb_t)(bool muted);

/**
 * @brief Hook up the buttons.
 *
 * @note Call after bsp_board_init(), which creates the button driver.
 */
esp_err_t hw_buttons_init(void);

/**
 * @brief Be told when the mute switch moves.
 */
void hw_buttons_set_mute_callback(hw_buttons_mute_changed_cb_t callback);

/**
 * @brief Current position of the mute switch.
 */
bool hw_buttons_is_muted(void);

#ifdef __cplusplus
}
#endif
