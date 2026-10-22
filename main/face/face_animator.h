/*
 * face_animator.h — Decides WHEN the face changes.
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 *
 * face_view knows how to draw a face. This module knows when to make it blink,
 * when to let it get bored and glance around, and how to flap the mouth in time
 * with whatever the speaker is playing.
 *
 * It runs entirely on LVGL timers, so there are no extra tasks and no locking
 * to think about inside the module itself. Calls made from other tasks (the
 * voice pipeline, mostly) must take the display lock first — the public
 * functions below are all safe to call that way.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "face_emotions.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Start blinking, idling and watching for speech.
 *
 * Call once, after face_view_create().
 */
esp_err_t face_animator_start(void);

/**
 * @brief Change the resting expression.
 *
 * The face glides into the new mood and stays there until told otherwise.
 * Blinking and idle antics continue on top of it.
 *
 * @param emotion      Mood to settle into.
 * @param duration_ms  Glide time; pass 0 for the configured default.
 */
void face_animator_set_emotion(face_emotion_t emotion, uint16_t duration_ms);

/**
 * @brief Which expression the face is currently resting in.
 */
face_emotion_t face_animator_get_emotion(void);

/**
 * @brief Play a short expression and then drift back to the resting one.
 *
 * Handy for reactions: a flash of surprise, a wink of acknowledgement.
 *
 * @param emotion  Expression to flash.
 * @param hold_ms  How long to hold it before returning.
 */
void face_animator_flash_emotion(face_emotion_t emotion, uint16_t hold_ms);

/**
 * @brief Perform one of the scripted idle antics, chosen at random.
 *
 * This is what a screen tap triggers. Interrupts any antic already running.
 */
void face_animator_play_random_antic(void);

/**
 * @brief Perform a specific antic by index (see face_antic_get_count()).
 */
void face_animator_play_antic(uint8_t index);

/**
 * @brief Blink once, right now.
 */
void face_animator_blink_now(void);

/**
 * @brief Start or stop the talking mouth.
 *
 * While speaking is enabled the mouth opening is refreshed several times a
 * second from the speaker's live output level, so the face mouths along with
 * whatever is being played. Idle antics are suspended for the duration.
 *
 * @param speaking  true when audio that represents the buddy's voice is playing.
 */
void face_animator_set_speaking(bool speaking);

/**
 * @brief Enable or disable spontaneous idle antics (a user setting).
 */
void face_animator_set_idle_antics_enabled(bool enabled);

/**
 * @brief Tell the animator that a human interacted, so it should wake up.
 *
 * Resets the "how long have I been ignored" timer that eventually makes the
 * buddy look sleepy.
 */
void face_animator_notice_activity(void);

#ifdef __cplusplus
}
#endif
