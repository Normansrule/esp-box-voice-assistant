/*
 * voice_cloud_brain.h — Free-form question answering, via an online service.
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 *
 * The chip cannot transcribe arbitrary speech on its own, so anything that is
 * not one of the offline phrases goes out to an OpenAI-compatible endpoint and
 * comes back as three things:
 *
 *   1. TRANSCRIPTION — the recorded question turned into text
 *   2. ANSWER        — a short reply written in the buddy's voice
 *   3. SPEECH        — that reply rendered back into MP3 audio
 *
 * All three steps run on a dedicated background task, so the face keeps
 * blinking and the buddy keeps listening while it waits for the network.
 *
 * The buddy also asks the model to prefix its answer with a mood in square
 * brackets — "[curious] Good question, ..." — which is parsed off and used to
 * set the expression. That is what makes cloud answers feel like they belong to
 * the same character as the offline ones.
 *
 * TWO PLACES THE ANSWERS CAN COME FROM
 * ------------------------------------
 * The same three requests work against two kinds of server, chosen by the brain
 * mode in Settings:
 *
 *   OpenAI     api.openai.com over HTTPS, with the owner's API key.
 *   Home AI    tools/home_brain/home_brain.py on a computer on the same Wi-Fi,
 *              over plain HTTP: Whisper for hearing, a small open model (via
 *              Ollama) for answering, Piper for the voice. No key, no account,
 *              no internet needed once it is installed.
 *
 * Both speak the same OpenAI-shaped API, which is why one client serves both.
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "face_emotions.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief What the cloud brain reports back as a request progresses.
 *
 * Every callback runs on the cloud brain's own task, never on the caller's.
 */
typedef struct {
    /** The question, once it has been transcribed. May be NULL on failure. */
    void (*on_question_transcribed)(const char *question);

    /** The written answer and the mood it should be delivered in. */
    void (*on_answer_ready)(const char *answer, face_emotion_t emotion);

    /**
     * Spoken audio is ready to play.
     *
     * The data stays valid until the next request starts, so it is safe to hand
     * straight to the speaker and return.
     */
    void (*on_speech_ready)(const void *mp3_data, size_t mp3_length);

    /** Something went wrong; the text is short enough to show on screen. */
    void (*on_failed)(const char *reason);

    /**
     * The request is over, whatever happened to it.
     *
     * Always called, exactly once per request. This is what lets the caller
     * resume listening without having to reason about which of the three steps
     * failed.
     *
     * @param audio_will_play  true when on_speech_ready was called and the
     *                         speaker is about to be busy.
     */
    void (*on_request_finished)(bool audio_will_play);
} voice_cloud_callbacks_t;

/**
 * @brief Start the background task that talks to the service.
 *
 * @param callbacks  Progress reports. Copied, so it need not outlive the call.
 */
esp_err_t voice_cloud_brain_start(const voice_cloud_callbacks_t *callbacks);

/**
 * @brief True when the brain the current mode uses could be asked right now.
 *
 * In Home AI mode that is voice_cloud_brain_home_is_available(), otherwise
 * voice_cloud_brain_openai_is_available().
 */
bool voice_cloud_brain_is_available(void);

/**
 * @brief True when there is an OpenAI key, the switch is on, and Wi-Fi is up.
 */
bool voice_cloud_brain_openai_is_available(void);

/**
 * @brief True when a home brain address is saved and Wi-Fi is up.
 *
 * Cannot tell whether that computer is switched on. A question finds out in
 * about a second (see BUDDY_HOME_BRAIN_PROBE_TIMEOUT_MS) rather than hanging.
 */
bool voice_cloud_brain_home_is_available(void);

/**
 * @brief Explain in a few words why the cloud brain is unavailable.
 *
 * @return A short reason, or NULL when it IS available.
 */
const char *voice_cloud_brain_explain_unavailability(void);

/**
 * @brief Send a recorded question off to be answered.
 *
 * Returns as soon as the request is queued. Only one request runs at a time;
 * a second one while the first is in flight is rejected.
 *
 * @param wav_data    A complete WAV file. Must stay valid until the request
 *                    completes — the recorder's buffer satisfies this.
 * @param wav_length  Length of the WAV file in bytes.
 */
esp_err_t voice_cloud_brain_ask(const uint8_t *wav_data, size_t wav_length);

/**
 * @brief Turn a line of text into speech, skipping the question entirely.
 *
 * This is how offline answers get a voice: the buddy already knows what it
 * wants to say, it just needs the text-to-speech half of the service. The audio
 * arrives through the same on_speech_ready callback as a full exchange.
 *
 * @param text  What to say. Copied, so the caller's buffer can be reused.
 */
esp_err_t voice_cloud_brain_speak(const char *text);

/**
 * @brief True while a request is in flight.
 */
bool voice_cloud_brain_is_busy(void);

/**
 * @brief Forget the running conversation and start a fresh one.
 */
void voice_cloud_brain_reset_conversation(void);

#ifdef __cplusplus
}
#endif
