/*
 * ui_speech_bubble.h — The caption bar along the bottom of the screen.
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 *
 * Shows what the buddy just said, and what it heard. The face slides up by a
 * couple of dozen pixels while the bubble is out and drops back down when it
 * goes away, which keeps the eyes and mouth clear of the text.
 *
 * The bubble matters more than it looks: it is the only way to follow the buddy
 * when the volume is down, and it is the first place to look when a cloud
 * request fails.
 */

#pragma once

#include <stdbool.h>
#include "esp_err.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Who is talking, which decides how the bubble is styled.
 */
typedef enum {
    UI_SPEECH_FROM_BUDDY = 0,  /*!< The buddy's own words, in Espressif red   */
    UI_SPEECH_FROM_USER,       /*!< What the buddy heard, in muted grey       */
    UI_SPEECH_STATUS,          /*!< A note about what is happening            */
} ui_speech_source_t;

/**
 * @brief Create the bubble, hidden, at the bottom of @p parent.
 *
 * @note Call with the display lock held.
 */
esp_err_t ui_speech_bubble_create(lv_obj_t *parent);

/**
 * @brief Show a line of text.
 *
 * @param source        Who is speaking.
 * @param text          What to show. Long text scrolls sideways.
 * @param auto_hide_ms  Hide after this many milliseconds; 0 keeps it up.
 */
void ui_speech_bubble_show(ui_speech_source_t source, const char *text, uint32_t auto_hide_ms);

/**
 * @brief Hide the bubble and let the face drop back to its resting position.
 */
void ui_speech_bubble_hide(void);

/**
 * @brief True while the bubble is on screen.
 */
bool ui_speech_bubble_is_visible(void);

#ifdef __cplusplus
}
#endif
