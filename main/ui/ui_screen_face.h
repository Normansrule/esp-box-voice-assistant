/*
 * ui_screen_face.h — The buddy's home screen.
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 *
 * A white field with the face filling it, a small sensor readout in the top
 * right, a settings gear in the bottom left, and a caption bar that slides in
 * from the bottom when there is something to read.
 *
 * Tapping anywhere that is not the gear makes the buddy react — which is the
 * whole point of it sitting on a desk.
 */

#pragma once

#include <stdbool.h>
#include "esp_err.h"
#include "face_emotions.h"
#include "ui_speech_bubble.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Build the home screen and start the face animating.
 *
 * Takes the display lock internally; call it from any task.
 */
esp_err_t ui_screen_face_create(void);

/**
 * @brief Change the buddy's expression from another task.
 *
 * Takes the display lock, so it is safe to call from the voice pipeline.
 */
void ui_screen_face_set_emotion(face_emotion_t emotion);

/**
 * @brief Show a caption, taking the display lock for you.
 *
 * @param source        Who is speaking.
 * @param text          The line to show.
 * @param auto_hide_ms  Hide after this long; 0 leaves it up.
 */
void ui_screen_face_show_caption(ui_speech_source_t source, const char *text,
                                 uint32_t auto_hide_ms);

/**
 * @brief Hide the caption bar, taking the display lock for you.
 */
void ui_screen_face_hide_caption(void);

/**
 * @brief Start or stop the talking mouth, taking the display lock for you.
 */
void ui_screen_face_set_speaking(bool speaking);

/**
 * @brief Play a random idle antic, taking the display lock for you.
 */
void ui_screen_face_play_antic(void);

/**
 * @brief Open the settings screen, taking the display lock for you.
 */
void ui_screen_face_open_settings(void);

/**
 * @brief Show the list of phrases the buddy understands.
 */
void ui_screen_face_open_examples(void);

#ifdef __cplusplus
}
#endif
