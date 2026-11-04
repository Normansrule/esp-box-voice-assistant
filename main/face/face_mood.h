/*
 * face_mood.h — How the buddy's mood drifts with time and touch.
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 *
 * STATUS: VERIFIED IN TEST (host tests). UNTESTED ON DEVICE.
 *
 * Two small pieces of judgement, kept free of LVGL and timers so the host
 * tests can pin them down exactly:
 *
 *   IDLE DRIFT   A mood is a reaction to something, so it should not last for
 *                ever. Left alone, a reply's expression fades back to neutral,
 *                then the buddy gets sleepy, and eventually it nods off. Any
 *                touch, wake word or arrival resets the clock (the animator's
 *                face_animator_notice_activity()).
 *
 *   TICKLES      One tap gets one antic, as it always has. Several quick taps
 *                in a row are a tickle and set off a fit of the giggles; keep
 *                going and the buddy scrunches up and asks you to stop.
 *
 * Neither invents an event. The inputs are elapsed time since the last real
 * interaction and the timestamps of real touches.
 */

#pragma once

#include <stdint.h>
#include "face_emotions.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief The resting expression after @p idle_ms with nobody interacting.
 *
 * Status expressions (listening, thinking, speaking, muted) are owned by the
 * code that set them and are returned unchanged.
 *
 * @param resting  The current resting expression.
 * @param idle_ms  Milliseconds since the last human interaction.
 * @return The expression the face should rest in now (may equal @p resting).
 */
face_emotion_t face_mood_idle_drift(face_emotion_t resting, uint32_t idle_ms);

/** What a tap should turn into. */
typedef enum {
    FACE_TICKLE_POKE = 0,   /*!< An ordinary tap: play a random antic        */
    FACE_TICKLE_GIGGLE,     /*!< Third quick tap or so: a fit of the giggles */
    FACE_TICKLE_ENOUGH,     /*!< Still going: scrunch up, "okay, okay!"      */
} face_tickle_reaction_t;

/** Running state for the tickle detector. Zero-initialise before first use. */
typedef struct {
    uint32_t last_tap_ms;
    uint8_t  streak;
} face_tickle_t;

/**
 * @brief Record one tap and decide how to react to it.
 *
 * @param tickle  Detector state.
 * @param now_ms  A free-running millisecond clock. Wrap-around is handled.
 */
face_tickle_reaction_t face_tickle_register(face_tickle_t *tickle, uint32_t now_ms);

#ifdef __cplusplus
}
#endif
