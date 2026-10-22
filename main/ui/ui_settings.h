/*
 * ui_settings.h — The configuration screens behind the bottom-left gear.
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 *
 * Everything the buddy can be told about itself lives here: how loud, how
 * bright, which language, which Wi-Fi network, which API key, where music comes
 * from, and what personality it should have. Changes apply immediately and are
 * written to flash as they are made, so nothing is lost to a power cut.
 *
 * There are three pages, and they stack:
 *
 *   main       a scrolling list of everything
 *   Wi-Fi      a list of nearby networks
 *   text entry a full-screen box and on-screen keyboard, reused by every
 *              setting that needs typing
 */

#pragma once

#include <stdbool.h>
#include "esp_err.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Create the settings button in the bottom-left corner of @p parent.
 *
 * The pages themselves are built on demand, so nothing is spent on them until
 * the button is actually pressed.
 *
 * @note Call with the display lock held.
 */
esp_err_t ui_settings_create_button(lv_obj_t *parent);

/**
 * @brief Open the settings, as if the gear had been tapped.
 *
 * @note Call with the display lock held.
 */
void ui_settings_open(void);

/**
 * @brief Close the settings and return to the face.
 */
void ui_settings_close(void);

/**
 * @brief Open Settings with the Question Examples screen already showing.
 *
 * The voice route into the phrase list: asking "what can you do" puts the whole
 * list on screen rather than reciting two examples and leaving the user to go
 * hunting for the rest.
 */
void ui_settings_open_examples(void);

/**
 * @brief True while any settings page is on screen.
 */
bool ui_settings_is_open(void);

#ifdef __cplusplus
}
#endif
