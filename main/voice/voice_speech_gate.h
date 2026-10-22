/*
 * voice_speech_gate.h — Deciding whether anything was actually said.
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 *
 * WHY THIS EXISTS
 * ---------------
 * When the wake word fires and no registered phrase matches, the recording is
 * sent to OpenAI. That is the right behaviour for a question. It is the wrong
 * behaviour for a cough, a door closing, the television, or the very common
 * case of somebody saying the wake word and then changing their mind — and each
 * one of those was a paid round trip, a "Thinking..." caption, and several
 * seconds of the buddy being deaf while it transcribed silence.
 *
 * So before paying for anything, ask a cheaper question: was there speech in
 * there at all?
 *
 * HOW IT DECIDES
 * --------------
 * Loudness over short blocks. Speech is not steady — it is bursts of energy
 * separated by gaps — so what matters is not the average level of the recording
 * but how MUCH of it was loud. A ten-second recording with one loud second is a
 * cough; the same recording with three loud seconds spread through it is a
 * sentence.
 *
 * This is deliberately not a voice activity detector. It cannot tell speech
 * from a slammed door, and it does not need to: the language model on the far
 * end sorts out what the words were. All this has to do is stop the buddy
 * paying to transcribe a room that nobody spoke in, and simple arithmetic on
 * samples the recorder is already copying does that for nothing.
 *
 * Pure arithmetic, no hardware, no ESP-IDF — the host tests exercise it with
 * synthesised silence, tones and clicks.
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * How many samples are judged together.
 *
 * 160 samples is 10 ms at 16 kHz — long enough to average out a single sample
 * of noise, short enough that the gaps between words are still visible as gaps.
 */
#define VOICE_GATE_BLOCK_SAMPLES        160

/** Running measurement of one recording. Zero-initialise, or reset it. */
typedef struct {
    uint32_t total_blocks;      /*!< Blocks measured so far                    */
    uint32_t loud_blocks;       /*!< ...of which were above the threshold      */
    int32_t  peak_amplitude;    /*!< Loudest single sample seen                */

    /* Partial block carried between calls, so buffer boundaries do not matter. */
    uint32_t pending_samples;
    uint64_t pending_energy;
} voice_speech_gate_t;

/* ------------------------------------------------------------------------ */
/* The room                                                                  */
/*                                                                           */
/* A fixed loudness threshold works in the room it was tuned in and nowhere   */
/* else. A quiet study sits near silence, so anything above it is speech; a   */
/* kitchen with an extractor fan can sit ABOVE the threshold all day, so      */
/* every wake word is followed by "speech" that is really just the fan, and   */
/* the box happily pays to transcribe it.                                     */
/*                                                                           */
/* So the room measures itself. While nobody is talking to it, the front end  */
/* is already handing frames to the recogniser, and those same frames are     */
/* passed through here to track how loud the room is when nothing is being    */
/* said. Speech then has to stand out from THAT, not from a number chosen     */
/* months ago on a different desk.                                            */
/* ------------------------------------------------------------------------ */

/**
 * @brief Feed idle microphone audio in, to learn the room.
 *
 * Call it with frames captured while the buddy is NOT in a listening window.
 * Cheap: one multiply per sample, on audio that was already in cache.
 */
void voice_speech_gate_observe_room(const int16_t *samples, size_t count);

/**
 * @brief The room's current background level, on the same 0-32767 scale.
 *
 * Reported in the boot log and the self test, because "how loud is it where
 * this box lives" is the number that explains both halves of the complaint —
 * ignoring you, and answering the television.
 */
uint32_t voice_speech_gate_room_level(void);

/**
 * @brief The loudness a block must reach right now to count as speech.
 *
 * The larger of the absolute floor and the room level times the margin. Useful
 * for logging: printing it next to the room level makes the gate's behaviour
 * something you can read rather than infer.
 */
uint32_t voice_speech_gate_threshold(void);

/**
 * @brief How far above the room speech has to be, as a multiple.
 *
 * Set from the noise profile in Settings. Passing zero restores the default.
 */
void voice_speech_gate_set_margin(uint8_t margin);

/** @brief Forget the room. Used at start-up and by the tests. */
void voice_speech_gate_forget_room(void);

/** @brief Start a fresh measurement. */
void voice_speech_gate_reset(voice_speech_gate_t *gate);

/**
 * @brief Measure another buffer of samples.
 *
 * Safe to call with any buffer length; partial blocks are carried over, so the
 * result does not depend on how the audio was chunked.
 *
 * @param gate     The running measurement.
 * @param samples  Mono 16-bit samples.
 * @param count    How many.
 */
void voice_speech_gate_add(voice_speech_gate_t *gate, const int16_t *samples, size_t count);

/**
 * @brief Seconds of the recording that were loud enough to be speech.
 *
 * @param gate         The measurement.
 * @param sample_rate  Samples per second the audio was captured at.
 */
float voice_speech_gate_speech_seconds(const voice_speech_gate_t *gate, uint32_t sample_rate);

/**
 * @brief Seconds measured in total, loud or not.
 */
float voice_speech_gate_total_seconds(const voice_speech_gate_t *gate, uint32_t sample_rate);

/**
 * @brief Is this worth sending to a service that charges per request?
 *
 * True when there is enough audio AND enough of it was loud. Both conditions
 * matter: a long recording of nothing is a room, and a very short loud one is a
 * cough or a chair.
 *
 * @param gate         The measurement.
 * @param sample_rate  Samples per second.
 */
bool voice_speech_gate_worth_asking(const voice_speech_gate_t *gate, uint32_t sample_rate);

#ifdef __cplusplus
}
#endif
