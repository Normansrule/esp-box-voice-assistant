/*
 * voice_recorder.h — Captures the question the user just asked.
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 *
 * When the wake word fires, the buddy starts stashing audio here. If one of the
 * offline phrases matches, the recording is thrown away and answered instantly.
 * If nothing matches, this buffer is what gets sent to the cloud for
 * transcription.
 *
 * The audio saved is the CLEANED output of the speech front end — noise
 * suppressed, single channel, 16 kHz — which is exactly the format speech-to-
 * text services want, and much better material for them than the raw mics.
 *
 * The buffer lives in external PSRAM, so ten seconds of audio costs no internal
 * RAM at all.
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Allocate the recording buffer in PSRAM.
 */
esp_err_t voice_recorder_init(void);

/**
 * @brief Throw away anything held and begin a new recording.
 */
void voice_recorder_start(void);

/**
 * @brief Stop recording. The captured audio stays available until the next start.
 */
void voice_recorder_stop(void);

/**
 * @brief True while audio is being captured.
 */
bool voice_recorder_is_recording(void);

/**
 * @brief Append one frame of cleaned audio.
 *
 * Called from the speech engine's fetch loop. Silently ignores the frame once
 * the buffer is full, so a rambling question cannot overflow anything.
 *
 * @param samples       Mono 16-bit samples at the capture sample rate.
 * @param sample_count  How many samples @p samples holds.
 */
void voice_recorder_append(const int16_t *samples, size_t sample_count);

/**
 * @brief How many seconds of audio are currently held.
 */
float voice_recorder_get_duration_seconds(void);

/**
 * @brief Was there enough speech in the last recording to be worth asking about?
 *
 * The test the buddy applies before spending money on a transcription. See
 * voice_speech_gate.h for what it measures and buddy_config.h for the two
 * thresholds it compares against.
 */
bool voice_recorder_heard_a_question(void);

/**
 * @brief Claim or release the recording buffer.
 *
 * Set while something is reading the WAV handed out by
 * voice_recorder_get_wav() — an upload in progress. While it is set, a new
 * wake word is still heard but nothing is written, so the bytes being read
 * cannot change underneath the reader.
 *
 * This is what allows the microphone to come back on before a slow cloud
 * request has finished, instead of the buddy staying deaf until it does.
 */
void voice_recorder_set_in_use(bool in_use);

/**
 * @brief How much of the last recording was loud enough to be speech, in seconds.
 */
float voice_recorder_get_speech_seconds(void);

/**
 * @brief The loudest single sample in the last recording, 0 to 32767.
 *
 * The crude measurement, and the trustworthy one. Unlike the speech seconds
 * above it involves no threshold, no room estimate and no noise profile, so it
 * cannot be mis-tuned into saying nothing was there. That is why it — and only
 * it — decides whether the buddy answers "I did not hear anything".
 */
int32_t voice_recorder_get_peak_amplitude(void);

/**
 * @brief Get the recording as a complete WAV file, header and all.
 *
 * The WAV header is written into the bytes reserved in front of the audio, so
 * nothing is copied and nothing extra is allocated.
 *
 * @param wav_data    Receives a pointer to the first byte of the WAV file.
 * @param wav_length  Receives the total file length in bytes.
 * @return ESP_OK, or ESP_ERR_INVALID_STATE when there is nothing worth sending.
 */
esp_err_t voice_recorder_get_wav(const uint8_t **wav_data, size_t *wav_length);

#ifdef __cplusplus
}
#endif
