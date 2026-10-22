/*
 * audio_resampler.h — Converting audio down to the rate the codec is pinned at.
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 *
 * WHY THE BUDDY NEEDS THIS
 * ------------------------
 * The ES8311 speaker and the ES7210 microphone array hang off the SAME I2S
 * port, and an I2S port has exactly ONE clock. Setting the speaker to 44.1 kHz
 * for a music file therefore sets the microphone to 44.1 kHz as well. You can
 * watch it happen in the log the instant a song starts:
 *
 *     I2S_IF: STD Mode 1 bits:16/16 channel:2 sample_rate:44100   <- speaker
 *     I2S_IF: STD Mode 0 bits:16/16 channel:2 sample_rate:44100   <- microphone
 *
 * WakeNet is a 16 kHz model and nothing tells it the rate moved. It carries on
 * taking 16000 samples per second out of a stream now arriving at 44100, so
 * every voice it hears is nearly three times too fast and almost two octaves
 * too high. "Hi E.S.P." cannot match. The buddy goes deaf for the length of the
 * song — including, infuriatingly, deaf to being asked to stop playing it.
 *
 * So the codec clock never moves. Anything recorded at a higher rate is
 * converted down to the capture rate here instead, in software, on its way to
 * the speaker. The cost is everything above half the capture rate; on a speaker
 * the size of a postage stamp that was mostly theoretical anyway, and it buys a
 * buddy that can hear you over its own music.
 *
 * Nothing in this file touches hardware, which is what makes it testable on a
 * laptop — see the "Sample rate conversion" group in tests/test_desk_buddy.c.
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Most interleaved channels this converter handles. */
#define AUDIO_RESAMPLER_MAX_CHANNELS    2

/**
 * @brief One conversion in progress.
 *
 * Treat the fields as private; they are in the header only so the struct can
 * live in static storage rather than being malloc'd on every track.
 */
typedef struct {
    uint32_t source_rate;       /*!< Rate the incoming audio was recorded at   */
    uint32_t target_rate;       /*!< Rate the codec is pinned to               */
    uint8_t  channels;          /*!< 1 or 2, interleaved                       */

    uint32_t step;              /*!< Q16 input frames to advance per output    */
    uint32_t phase;             /*!< Q16 position in the stream                */
    int16_t  previous[AUDIO_RESAMPLER_MAX_CHANNELS];

    int32_t  lowpass_first[AUDIO_RESAMPLER_MAX_CHANNELS];
    int32_t  lowpass_second[AUDIO_RESAMPLER_MAX_CHANNELS];
    int32_t  lowpass_k;         /*!< Q15 one-pole coefficient                  */
} audio_resampler_t;

/**
 * @brief Begin converting a new track.
 *
 * Safe to call with a source rate equal to (or below) the target, which is the
 * common case — it simply marks the converter as not needed.
 *
 * @param resampler    State to initialise. Must not be NULL.
 * @param source_rate  Rate the audio was recorded at, in Hz.
 * @param target_rate  Rate the codec is running at, in Hz.
 * @param channels     Interleaved channel count; clamped to 1 or 2.
 */
void audio_resampler_begin(audio_resampler_t *resampler,
                           uint32_t source_rate,
                           uint32_t target_rate,
                           uint8_t channels);

/**
 * @brief Whether audio actually has to be converted, or can be played as-is.
 */
bool audio_resampler_is_needed(const audio_resampler_t *resampler);

/**
 * @brief Upper bound on the frames one input buffer can produce.
 *
 * Lets a caller size a scratch buffer without guessing. Because this converter
 * only ever goes downwards, the answer never exceeds @p input_frames.
 */
size_t audio_resampler_max_output_frames(const audio_resampler_t *resampler,
                                         size_t input_frames);

/**
 * @brief Convert one buffer.
 *
 * @param resampler        Conversion state.
 * @param input            Interleaved 16-bit PCM at the source rate. LOW-PASSED
 *                         IN PLACE, so the caller's buffer is modified.
 * @param input_frames     Frames available in @p input.
 * @param output           Where converted frames are written.
 * @param output_capacity  Frames of room in @p output. Must be at least
 *                         audio_resampler_max_output_frames(); anything beyond
 *                         that is simply never used.
 * @return Frames written to @p output.
 */
size_t audio_resampler_process(audio_resampler_t *resampler,
                               int16_t *input, size_t input_frames,
                               int16_t *output, size_t output_capacity);

#ifdef __cplusplus
}
#endif
