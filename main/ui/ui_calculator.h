/*
 * ui_calculator.h — The keypad, so any sum can be done with nothing connected.
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 *
 * The buddy answers spoken sums, but only the ones registered with the phrase
 * recogniser in advance — that is how MultiNet works, and no arrangement of a
 * phrase list covers arbitrary arithmetic. This is the answer to "let me ask it
 * anything": tap the sum, and it works it out on the chip and SAYS the answer
 * out loud, using the same recorded number words the clock and thermometer use.
 *
 * The arithmetic itself lives in core/buddy_calculator.c, which knows nothing
 * about screens and is exercised directly by the host tests. This file is only
 * the buttons and the speaking.
 */

#pragma once

#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Show the calculator, full screen.
 *
 * Safe to call when it is already open — it does nothing. Takes the display
 * lock itself, so it can be called from any task.
 */
esp_err_t ui_calculator_open(void);

/** @brief Close it and go back to the face. */
void ui_calculator_close(void);

/** @brief True while the calculator is the thing on screen. */
bool ui_calculator_is_open(void);

#ifdef __cplusplus
}
#endif
