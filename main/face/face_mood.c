/*
 * face_mood.c — Idle drift and tickle detection. See face_mood.h.
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */

#include <stdbool.h>
#include <stddef.h>

#include "buddy_config.h"
#include "face_mood.h"

static bool is_owned_by_someone_else(face_emotion_t emotion)
{
    /*
     * These say what the device is DOING, not how it feels. Fading "listening"
     * to neutral while the microphone is open, or "muted" to sleepy while the
     * switch is on, would make the face lie about the device.
     */
    return emotion == FACE_EMOTION_LISTENING ||
           emotion == FACE_EMOTION_THINKING  ||
           emotion == FACE_EMOTION_SPEAKING  ||
           face_emotion_is_status_only(emotion);
}

face_emotion_t face_mood_idle_drift(face_emotion_t resting, uint32_t idle_ms)
{
    if (resting < 0 || resting >= FACE_EMOTION_COUNT) {
        return FACE_EMOTION_NEUTRAL;
    }
    if (is_owned_by_someone_else(resting)) {
        return resting;
    }

    const uint32_t minute_ms = 60u * 1000u;

    if (idle_ms >= (uint32_t)FACE_ASLEEP_AFTER_MINUTES * minute_ms) {
        return FACE_EMOTION_ASLEEP;
    }
    if (idle_ms >= (uint32_t)FACE_SLEEPY_AFTER_MINUTES * minute_ms) {
        /* Never wake back up to merely sleepy just because time passed. */
        return (resting == FACE_EMOTION_ASLEEP) ? FACE_EMOTION_ASLEEP : FACE_EMOTION_SLEEPY;
    }
    if (idle_ms >= (uint32_t)FACE_MOOD_FADE_AFTER_MS &&
        resting != FACE_EMOTION_SLEEPY && resting != FACE_EMOTION_ASLEEP) {
        return FACE_EMOTION_NEUTRAL;
    }
    return resting;
}

face_tickle_reaction_t face_tickle_register(face_tickle_t *tickle, uint32_t now_ms)
{
    if (tickle == NULL) {
        return FACE_TICKLE_POKE;
    }

    /* Unsigned subtraction gives the right gap across a clock wrap. */
    const uint32_t gap_ms = now_ms - tickle->last_tap_ms;
    if (tickle->streak == 0 || gap_ms > FACE_TICKLE_GAP_MS) {
        tickle->streak = 0;
    }
    tickle->last_tap_ms = now_ms;

    if (tickle->streak < UINT8_MAX) {
        tickle->streak++;
    }

    if (tickle->streak >= FACE_TICKLE_ENOUGH_TAPS) {
        tickle->streak = 0;     /* start over: the next tap is a fresh poke */
        return FACE_TICKLE_ENOUGH;
    }
    if (tickle->streak >= FACE_TICKLE_GIGGLE_TAPS) {
        return FACE_TICKLE_GIGGLE;
    }
    return FACE_TICKLE_POKE;
}
