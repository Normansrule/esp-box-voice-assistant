/*
 * voice_assistant.h — The conductor.
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 *
 * Every other module does exactly one thing and knows nothing about the rest.
 * This one knows about all of them, and its only job is deciding what happens
 * next. If you want to understand how the buddy behaves, read this file — the
 * conversation flow lives here and nowhere else.
 *
 * THE FLOW
 * --------
 *   1. WakeNet hears "Hi E.S.P."
 *      -> chime, face turns attentive, music ducks, recording starts
 *
 *   2a. MultiNet matches one of the offline phrases
 *      -> the offline brain answers instantly, the face reacts, done
 *
 *   2b. The listening window closes with no match
 *      -> if the cloud brain is available, the recording is sent off;
 *         otherwise the buddy apologises and explains what it needs
 *
 *   3. The cloud answers
 *      -> the question and answer appear as captions, the mood tag sets the
 *         expression, the spoken audio plays, and the mouth moves with it
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "voice_local_brain.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Start listening and wire every module together.
 *
 * Everything it depends on must already be initialised: display, speaker,
 * microphone, sensors, music, and (optionally) the network.
 */
esp_err_t voice_assistant_start(void);

/**
 * @brief Say something out loud and show it on screen.
 *
 * The single route for anything the buddy "says", wherever the words came
 * from. It shows the caption, sets the expression, and speaks the words when
 * text to speech is available.
 *
 * @return true when spoken audio was queued, meaning the speaker is about to be
 *         busy and the caller should leave audio alone.
 */
bool voice_assistant_deliver_reply(const voice_reply_t *reply);

/**
 * @brief Cloud requests left in the current hour.
 *
 * UINT16_MAX when no ceiling is set. Shown in Settings so the allowance is a
 * number the owner can watch rather than a surprise on a bill.
 */
uint16_t voice_assistant_requests_remaining(void);

/**
 * @brief Claim one request from the hourly allowance.
 *
 * @return false when the allowance is spent and the request must not go ahead.
 */
bool voice_assistant_spend_request(void);

#ifdef __cplusplus
}
#endif
