/*
 * voice_speech_gate.c — Deciding whether anything was actually said.
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 *
 * See voice_speech_gate.h for why this exists. The thresholds are in
 * buddy_config.h, because they are the numbers worth adjusting from a log.
 */

#include "buddy_config.h"
#include "voice_speech_gate.h"

/* ------------------------------------------------------------------------ */
/* What the room sounds like when nobody is talking                          */
/* ------------------------------------------------------------------------ */

/**
 * A running estimate of the room's background level, as a MEAN SQUARE.
 *
 * Kept squared throughout so that judging a block against it is a comparison
 * rather than two square roots, on a path that runs for every frame of
 * microphone audio all day.
 */
static uint64_t s_room_mean_square;

/** True once anything has been observed, so an unheard room is not "silent". */
static bool s_room_known;

/** Partial block carried between calls, exactly as the per-recording gate does. */
static uint32_t s_room_pending_samples;
static uint64_t s_room_pending_energy;

/** How far above the room speech must sit. Set from the noise profile. */
static uint8_t s_margin = BUDDY_SPEECH_OVER_ROOM_MARGIN;

/**
 * @brief Fold one finished block into the room estimate.
 *
 * Asymmetric on purpose. A block QUIETER than the current estimate pulls it
 * down quickly, because the quietest thing heard recently is the best evidence
 * of what the room actually sounds like. A louder block pushes it up only
 * slightly — otherwise somebody talking, or a song playing, would be learned as
 * "the room", the threshold would climb to match, and the box would go deaf to
 * the very thing it was listening for.
 */
static void fold_into_room(uint64_t block_mean_square)
{
    if (!s_room_known) {
        s_room_mean_square = block_mean_square;
        s_room_known = true;
        return;
    }

    if (block_mean_square < s_room_mean_square) {
        /* Down fast: a quarter of the way each block. */
        s_room_mean_square -= (s_room_mean_square - block_mean_square) / 4u;
    } else {
        /* Up slow: a sixty-fourth. */
        s_room_mean_square += (block_mean_square - s_room_mean_square) / 64u;
    }
}

void voice_speech_gate_observe_room(const int16_t *samples, size_t count)
{
    if (samples == NULL) {
        return;
    }

    for (size_t i = 0; i < count; i++) {
        const int32_t sample = samples[i];
        s_room_pending_energy += (uint64_t)((int64_t)sample * (int64_t)sample);
        s_room_pending_samples++;

        if (s_room_pending_samples >= VOICE_GATE_BLOCK_SAMPLES) {
            fold_into_room(s_room_pending_energy / s_room_pending_samples);
            s_room_pending_samples = 0;
            s_room_pending_energy = 0;
        }
    }
}

/** @brief Integer square root, so the level can be reported on the sample scale. */
static uint32_t isqrt64(uint64_t value)
{
    uint64_t result = 0;
    uint64_t bit = 1ULL << 40;

    while (bit > value) {
        bit >>= 2;
    }
    while (bit != 0) {
        if (value >= result + bit) {
            value -= result + bit;
            result = (result >> 1) + bit;
        } else {
            result >>= 1;
        }
        bit >>= 2;
    }
    return (uint32_t)result;
}

uint32_t voice_speech_gate_room_level(void)
{
    return s_room_known ? isqrt64(s_room_mean_square) : 0;
}

/** @brief The mean-square a block must reach to count as speech. */
static uint64_t speech_threshold_mean_square(void)
{
    const uint64_t absolute = (uint64_t)BUDDY_SPEECH_LOUDNESS_THRESHOLD *
                              (uint64_t)BUDDY_SPEECH_LOUDNESS_THRESHOLD;

    if (!s_room_known) {
        return absolute;
    }

    /*
     * The margin is a multiple of AMPLITUDE, so it is squared to compare
     * against mean squares. Six times the amplitude is about sixteen decibels
     * over the room — comfortably above a fan or a distant television, and
     * comfortably below anyone speaking to the box on purpose.
     */
    const uint64_t over_room = s_room_mean_square * (uint64_t)s_margin * (uint64_t)s_margin;

    /*
     * The absolute floor still applies. In a silent room the estimate goes to
     * almost nothing, and a threshold of "a bit more than nothing" would count
     * the microphone's own hiss as speech.
     */
    uint64_t threshold = (over_room > absolute) ? over_room : absolute;

    /*
     * And a ceiling, which is the more important of the two.
     *
     * The floor stops a silent room hearing hiss; getting it wrong means a few
     * wasted requests. The ceiling stops a LOUD room — or a loud room plus the
     * Noisy-room profile, which multiplies energy a hundredfold — from setting
     * a bar no human voice can clear. Getting that wrong means a box that never
     * answers again and gives no clue why, which is the worse failure by a wide
     * margin. Past this point the gate has stopped being useful, so it stops
     * being an obstacle instead of silently becoming an absolute one.
     */
    const uint64_t ceiling = (uint64_t)BUDDY_SPEECH_THRESHOLD_CEILING *
                             (uint64_t)BUDDY_SPEECH_THRESHOLD_CEILING;

    if (threshold > ceiling) {
        threshold = ceiling;
    }

    return threshold;
}

uint32_t voice_speech_gate_threshold(void)
{
    return isqrt64(speech_threshold_mean_square());
}

void voice_speech_gate_set_margin(uint8_t margin)
{
    s_margin = (margin > 0) ? margin : BUDDY_SPEECH_OVER_ROOM_MARGIN;
}

void voice_speech_gate_forget_room(void)
{
    s_room_mean_square = 0;
    s_room_known = false;
    s_room_pending_samples = 0;
    s_room_pending_energy = 0;
}

void voice_speech_gate_reset(voice_speech_gate_t *gate)
{
    if (gate == NULL) {
        return;
    }

    gate->total_blocks    = 0;
    gate->loud_blocks     = 0;
    gate->peak_amplitude  = 0;
    gate->pending_samples = 0;
    gate->pending_energy  = 0;
}

/**
 * @brief Judge one finished block and fold it into the totals.
 *
 * Mean square rather than root mean square: the square root would be the
 * conventional way to express a level, and comparing squares against a squared
 * threshold gives exactly the same answer without the arithmetic. This runs on
 * every buffer of microphone audio, so it is worth keeping cheap.
 */
static void finish_block(voice_speech_gate_t *gate)
{
    if (gate->pending_samples == 0) {
        return;
    }

    const uint64_t mean_square = gate->pending_energy / gate->pending_samples;

    /*
     * Judged against the room, not against a constant. See the long note at
     * the top of this file: a threshold that works in a study is deaf in a
     * kitchen, and one that works in a kitchen answers the television.
     */
    const uint64_t threshold = speech_threshold_mean_square();

    gate->total_blocks++;
    if (mean_square >= threshold) {
        gate->loud_blocks++;
    }

    gate->pending_samples = 0;
    gate->pending_energy  = 0;
}

void voice_speech_gate_add(voice_speech_gate_t *gate, const int16_t *samples, size_t count)
{
    if (gate == NULL || samples == NULL) {
        return;
    }

    for (size_t i = 0; i < count; i++) {
        const int32_t sample = samples[i];

        /*
         * The magnitude, guarding the one value that cannot be negated. INT16_MIN
         * has no positive counterpart in 16 bits, so -(-32768) is undefined
         * behaviour in the obvious spelling of this.
         */
        const int32_t magnitude = (sample < 0) ? -(int32_t)sample : sample;
        if (magnitude > gate->peak_amplitude) {
            gate->peak_amplitude = magnitude;
        }

        gate->pending_energy += (uint64_t)((int64_t)sample * (int64_t)sample);
        gate->pending_samples++;

        if (gate->pending_samples >= VOICE_GATE_BLOCK_SAMPLES) {
            finish_block(gate);
        }
    }
}

float voice_speech_gate_speech_seconds(const voice_speech_gate_t *gate, uint32_t sample_rate)
{
    if (gate == NULL || sample_rate == 0) {
        return 0.0f;
    }
    return (float)gate->loud_blocks * (float)VOICE_GATE_BLOCK_SAMPLES / (float)sample_rate;
}

float voice_speech_gate_total_seconds(const voice_speech_gate_t *gate, uint32_t sample_rate)
{
    if (gate == NULL || sample_rate == 0) {
        return 0.0f;
    }
    return (float)gate->total_blocks * (float)VOICE_GATE_BLOCK_SAMPLES / (float)sample_rate;
}

bool voice_speech_gate_worth_asking(const voice_speech_gate_t *gate, uint32_t sample_rate)
{
    if (gate == NULL || sample_rate == 0) {
        return false;
    }

    const float total  = voice_speech_gate_total_seconds(gate, sample_rate);
    const float speech = voice_speech_gate_speech_seconds(gate, sample_rate);

    /*
     * Both tests, and they catch different things.
     *
     * The length test rejects the wake word being said and then nothing —
     * common, and there is no question in it to answer. The speech test rejects
     * a long recording of a quiet room, which is what a wake word triggered by
     * the television produces.
     */
    return (total >= BUDDY_MIN_QUESTION_SECONDS) &&
           (speech >= BUDDY_MIN_SPEECH_SECONDS);
}
