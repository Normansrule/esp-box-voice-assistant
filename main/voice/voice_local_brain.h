/*
 * voice_local_brain.h — Answers the buddy can give with no internet at all.
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 *
 * Every phrase in voice_commands.c lands here. This module works out what to
 * say, picks a matching expression, and carries out anything it owns directly
 * (music transport, volume). It never draws — the caller does that.
 *
 * Keeping the offline answers separate from the cloud ones means the buddy is
 * still useful and still fun with the Wi-Fi unplugged.
 */

#pragma once

#include <stdbool.h>
#include "buddy_config.h"
#include "face_emotions.h"
#include "voice_commands.h"
#include "voice_speech_parts.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief What the buddy wants to say, and how it wants to look while saying it.
 */
typedef struct {
    char           text[BUDDY_REPLY_TEXT_MAX_LEN];
    face_emotion_t emotion;
    bool           speak_aloud;    /*!< Try text-to-speech if it is available   */
    bool           open_settings;  /*!< Caller should show the settings screen  */
    bool           open_examples;  /*!< Caller should show the phrase list       */
    bool           open_calculator;/*!< Caller should show the keypad            */
    bool           play_antic;     /*!< Caller should trigger a random antic    */

    /**
     * How to say this reply out loud when it has no recording of its own.
     *
     * Empty for the great majority of replies, which are fixed sentences and
     * are found by hashing `text` directly. Filled in for the answers that are
     * worked out — the time, a temperature, a countdown — where `text` is
     * unique to this moment and no recording of it could exist. See
     * voice_speech_parts.h.
     */
    voice_speech_plan_t speech;
} voice_reply_t;

/**
 * @brief Prepare the offline answer bank.
 */
void voice_local_brain_init(void);

/**
 * @brief Answer one recognised phrase.
 *
 * @param action  What was recognised.
 * @param phrase  The exact words that matched. Only VOICE_ACTION_DO_MATH needs
 *                them — every other action is answered from the action alone —
 *                but passing them always keeps the call site simple. May be
 *                NULL.
 * @param reply   Filled in with the answer.
 * @return true when the action was handled; false when it is unknown, in which
 *         case the caller should fall through to the cloud brain.
 */
bool voice_local_brain_answer(voice_action_t action, const char *phrase, voice_reply_t *reply);

/**
 * @brief Answer a question if — and only if — it is a sum.
 *
 * Used in two places: on phrases matched offline, and on the transcript of a
 * cloud question just before it would have been sent to the language model. The
 * second one means "what is seven times eight" is answered instantly and for
 * nothing even when the buddy is online.
 *
 * @param question  Free text, e.g. "what is 12 times 12".
 * @param reply     Filled in with the answer when the question is a sum.
 * @return true when it was a sum this buddy could work out.
 */
bool voice_local_brain_try_math(const char *question, voice_reply_t *reply);

/**
 * @brief Which spoken action asks for a given expression.
 *
 * Lets the settings screen show the phrase next to each face without keeping a
 * second copy of the mapping that could drift out of step with this one.
 *
 * @param emotion  The expression to look up.
 * @return The action that requests it, or VOICE_ACTION_NONE when the expression
 *         is one the buddy only uses by itself (listening, thinking, asleep).
 */
voice_action_t voice_local_brain_action_for_emotion(face_emotion_t emotion);

/**
 * @brief Compose the "I tried to look that up and could not" apology.
 *
 * For a REAL failure: the online brain is switched on and something went wrong
 * with it — no Wi-Fi, a rejected key, no answer. Use it only then. An online
 * brain that is off because nobody wants it is not a failure and must not be
 * reported as one; use voice_local_brain_compose_did_not_understand() instead.
 *
 * @param reason  Short explanation, e.g. "I am not on Wi-Fi". May be NULL.
 * @param reply   Filled in with the apology.
 */
void voice_local_brain_compose_offline_apology(const char *reason, voice_reply_t *reply);

/**
 * @brief Compose the "that is not one of my phrases" reply.
 *
 * The normal answer on a box running offline by choice, and so the reply the
 * user hears most often. It names something the buddy CAN do rather than
 * something it cannot, and unlike the apology above it is spoken aloud — each
 * alternative is a fixed sentence with a recorded clip on flash.
 *
 * @param reply  Filled in.
 */
void voice_local_brain_compose_did_not_understand(voice_reply_t *reply);

#ifdef __cplusplus
}
#endif
