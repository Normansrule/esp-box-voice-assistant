/*
 * audio_prompts.h — The little sounds the buddy makes.
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 *
 * These are the non-verbal cues that tell the user what state the buddy is in:
 * a rising chirp when it starts listening, a soft confirmation when it
 * understood, a low buzz when something went wrong. They are short WAV files in
 * the SPIFFS image and they play synchronously, because they last well under a
 * second and blocking is simpler than tracking yet another callback.
 */

#pragma once

#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief The available sounds.
 */
typedef enum {
    AUDIO_PROMPT_WAKE = 0,   /*!< Rising chirp: "I am listening"               */
    AUDIO_PROMPT_UNDERSTOOD, /*!< Soft two-tone: "got it"                      */
    AUDIO_PROMPT_THINKING,   /*!< Gentle blip: "asking the internet"           */
    AUDIO_PROMPT_ERROR,      /*!< Falling tone: "that did not work"            */
    AUDIO_PROMPT_COUNT,
} audio_prompt_t;

/**
 * @brief Check which prompt files are present on the filesystem.
 *
 * A missing file is not an error — that prompt just stays silent.
 */
esp_err_t audio_prompts_init(void);

/**
 * @brief Play a prompt and return once it has finished.
 *
 * Does nothing when chimes are switched off in the settings.
 */
void audio_prompts_play(audio_prompt_t prompt);

/**
 * @brief How many of the prompt files were found on the filesystem.
 *
 * Anything less than AUDIO_PROMPT_COUNT means the storage partition is out of
 * date. Used by the self test.
 */
uint8_t audio_prompts_get_available_count(void);

#ifdef __cplusplus
}
#endif
