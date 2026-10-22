/*
 * audio_resampler.c — Converting audio down to the rate the codec is pinned at.
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 *
 * The header explains WHY this exists. This file is just the arithmetic, kept
 * free of any ESP-IDF dependency so the host tests can exercise it directly.
 */

#include <math.h>

#include "audio_resampler.h"

/*
 * Anti-alias corner, as a fraction of the TARGET rate.
 *
 * 0.375 puts the corner at 6 kHz when converting down to 16 kHz — comfortably
 * under the 8 kHz output Nyquist, with enough room above it that the filter has
 * actually started rolling off by the time anything can fold back.
 */
#define CORNER_FRACTION_OF_TARGET   0.375f

/** Q15 coefficient meaning "let everything through". */
#define LOWPASS_PASSTHROUGH         32768

/** Floor on the coefficient, so an absurd source rate cannot stall the filter. */
#define LOWPASS_MINIMUM             655        /* about 0.02 */

static uint8_t clamp_channels(uint8_t channels)
{
    if (channels < 1) {
        return 1;
    }
    if (channels > AUDIO_RESAMPLER_MAX_CHANNELS) {
        return AUDIO_RESAMPLER_MAX_CHANNELS;
    }
    return channels;
}

void audio_resampler_begin(audio_resampler_t *resampler,
                           uint32_t source_rate,
                           uint32_t target_rate,
                           uint8_t channels)
{
    if (resampler == NULL) {
        return;
    }

    resampler->target_rate = (target_rate > 0) ? target_rate : 1;
    resampler->source_rate = (source_rate > 0) ? source_rate : resampler->target_rate;
    resampler->channels    = clamp_channels(channels);

    resampler->phase = 0;
    for (uint8_t channel = 0; channel < AUDIO_RESAMPLER_MAX_CHANNELS; channel++) {
        resampler->previous[channel]      = 0;
        resampler->lowpass_first[channel] = 0;
        resampler->lowpass_second[channel] = 0;
    }

    /* Q16: how far to walk through the input for every output frame. */
    resampler->step = (uint32_t)(((uint64_t)resampler->source_rate << 16)
                                 / resampler->target_rate);

    if (resampler->source_rate <= resampler->target_rate) {
        /* Nothing to throw away, so nothing needs filtering out first. */
        resampler->lowpass_k = LOWPASS_PASSTHROUGH;
        return;
    }

    const float corner = CORNER_FRACTION_OF_TARGET * (float)resampler->target_rate;
    float alpha = 1.0f - expf(-2.0f * 3.14159265f * corner / (float)resampler->source_rate);

    if (alpha > 1.0f) {
        alpha = 1.0f;
    }

    int32_t coefficient = (int32_t)(alpha * 32768.0f);
    if (coefficient < LOWPASS_MINIMUM) {
        coefficient = LOWPASS_MINIMUM;
    }
    if (coefficient > LOWPASS_PASSTHROUGH) {
        coefficient = LOWPASS_PASSTHROUGH;
    }
    resampler->lowpass_k = coefficient;
}

bool audio_resampler_is_needed(const audio_resampler_t *resampler)
{
    if (resampler == NULL) {
        return false;
    }
    return resampler->source_rate > resampler->target_rate;
}

size_t audio_resampler_max_output_frames(const audio_resampler_t *resampler,
                                         size_t input_frames)
{
    if (resampler == NULL) {
        return 0;
    }

    /*
     * This converter only ever goes downwards, so one input buffer can never
     * produce more frames than it contains. Two spare frames cover the carried
     * fraction landing right on a boundary.
     */
    if (!audio_resampler_is_needed(resampler)) {
        return input_frames;
    }
    return input_frames + 2;
}

/**
 * @brief Low-pass one buffer in place, at the SOURCE rate.
 *
 * This has to happen before anything is thrown away. Decimating without it
 * folds every frequency above the output Nyquist back down into the audible
 * band as a metallic wash — the classic "underwater robot" sound. Two one-pole
 * sections give 12 dB per octave: not a brick wall, but enough that the result
 * is pleasant rather than gritty.
 */
static void lowpass_in_place(audio_resampler_t *resampler,
                             int16_t *frames, size_t frame_count)
{
    const uint8_t channels = resampler->channels;

    if (resampler->lowpass_k >= LOWPASS_PASSTHROUGH) {
        return;
    }

    for (size_t frame = 0; frame < frame_count; frame++) {
        for (uint8_t channel = 0; channel < channels; channel++) {
            const size_t slot = (frame * channels) + channel;

            resampler->lowpass_first[channel] +=
                (((int32_t)frames[slot] - resampler->lowpass_first[channel])
                 * resampler->lowpass_k) >> 15;

            resampler->lowpass_second[channel] +=
                ((resampler->lowpass_first[channel] - resampler->lowpass_second[channel])
                 * resampler->lowpass_k) >> 15;

            frames[slot] = (int16_t)resampler->lowpass_second[channel];
        }
    }
}

size_t audio_resampler_process(audio_resampler_t *resampler,
                               int16_t *input, size_t input_frames,
                               int16_t *output, size_t output_capacity)
{
    if (resampler == NULL || input == NULL || output == NULL || input_frames == 0) {
        return 0;
    }

    const uint8_t channels = resampler->channels;

    lowpass_in_place(resampler, input, input_frames);

    /*
     * Interpolation walks a VIRTUAL stream made of the last frame of the
     * previous buffer followed by this one:
     *
     *     virtual[0]   = the frame carried over from last time
     *     virtual[1..] = this buffer
     *
     * which is what lets an output sample land BETWEEN two input buffers with
     * no special casing. Whatever fraction of a frame is left at the end
     * carries into the next call, so there is no click at the seam.
     */
    size_t produced = 0;

    while (produced < output_capacity) {
        const size_t index = resampler->phase >> 16;

        /* Needs virtual[index] and virtual[index + 1]; the highest is input_frames. */
        if (index + 1 > input_frames) {
            break;
        }

        const int32_t fraction = (int32_t)(resampler->phase & 0xFFFFu);

        for (uint8_t channel = 0; channel < channels; channel++) {
            const int32_t here = (index == 0)
                                 ? resampler->previous[channel]
                                 : input[((index - 1) * channels) + channel];
            const int32_t next = input[(index * channels) + channel];

            output[(produced * channels) + channel] =
                (int16_t)(here + (((next - here) * fraction) >> 16));
        }

        resampler->phase += resampler->step;
        produced++;
    }

    /* Slide the window forward by exactly this buffer, keeping the remainder. */
    const uint32_t consumed = (uint32_t)input_frames << 16;
    resampler->phase = (resampler->phase > consumed) ? (resampler->phase - consumed) : 0;

    for (uint8_t channel = 0; channel < channels; channel++) {
        resampler->previous[channel] = input[((input_frames - 1) * channels) + channel];
    }

    return produced;
}
