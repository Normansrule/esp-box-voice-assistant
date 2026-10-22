/*
 * voice_speech_parts.h — Speaking a sentence that has a number in it.
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 *
 * THE PROBLEM
 * -----------
 * The offline voice finds a recording by hashing the exact sentence it is about
 * to say. That covers every answer the buddy knows by heart, and none of the
 * ones it works out: the time, the date, the temperature, the humidity, the
 * volume, how long is left on the timer, the answer to a sum. Those are the
 * questions people ask most, and until now the buddy mouthed them in silence.
 *
 * THE FIX
 * -------
 * Say them in pieces. "It is" + "three" + "forty seven" + "in the afternoon" is
 * four recordings, and the pieces recombine into the eighty-six thousand
 * different times of day that could never each have their own.
 *
 * The pieces are concatenated into one buffer and decoded as a single stream
 * rather than played one file after another — see audio_voice.c. MP3 frames are
 * self-contained, so a sentence assembled this way plays as one continuous
 * utterance instead of four clips with gaps between them.
 *
 * WHAT MAKES THIS SAFE
 * --------------------
 * Every fragment below is a string literal in voice_speech_parts.c, and the
 * clip generator records every literal in that file. Reword one and its
 * recording changes name with it: the lookup misses, the sentence is shown but
 * not spoken, and the log says which fragment was missing. The buddy can go
 * quiet, but it can never say the wrong number.
 *
 * ADDING A SPOKEN ANSWER
 * ----------------------
 *   1. Add whatever fragments it needs to voice_speech_parts.c.
 *   2. Build the part list in voice_local_brain.c beside the on-screen text.
 *   3. Re-run scripts/make_voice_clips.py and rebuild.
 * The host tests check that every fragment named here exists in the table.
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Most fragments one spoken sentence may be built from.
 *
 * Raised from eight in v3.6 for the calculator. A sentence used to be a timer
 * reading at six parts; now it can be a six-figure answer with a decimal —
 * "four hundred and seven thousand two hundred and nine point five" is twelve
 * fragments on its own. Sixteen covers the largest number this speaks with
 * room to put a phrase in front of it, and costs 64 bytes in voice_reply_t.
 */
#define VOICE_SPEECH_MAX_PARTS      16

/**
 * @brief An assembled spoken sentence: a list of fragments to say in order.
 *
 * `count == 0` means "nothing assembled" — the ordinary case, where the reply
 * is a fixed sentence with a recording of its own.
 */
typedef struct {
    const char *parts[VOICE_SPEECH_MAX_PARTS];
    uint8_t     count;
} voice_speech_plan_t;

/* --- Building a plan ---------------------------------------------------- */

/** @brief Empty the plan. */
void voice_speech_plan_reset(voice_speech_plan_t *plan);

/**
 * @brief Append one fragment.
 *
 * A NULL fragment, or one that overflows the list, INVALIDATES the whole plan
 * rather than being skipped. Half a sentence said aloud is worse than none:
 * "it is three in the afternoon" when the time is 3:47 would be confidently
 * wrong, and this module's entire job is to make that impossible.
 *
 * @return true while the plan is still usable.
 */
bool voice_speech_plan_add(voice_speech_plan_t *plan, const char *fragment);

/**
 * @brief Append a whole number, spelled out.
 *
 * Numbers up to VOICE_NUMBER_WORDS_MAX are one recording. Larger ones are
 * COMPOSED from the same bank plus "hundred" and "thousand", so 407209 becomes
 * six fragments rather than needing a recording of its own — which is what
 * makes the calculator able to say its answers without half a megabyte of
 * clips. Negative values get "minus" in front.
 *
 * Invalidates the plan for anything beyond a million, or when the parts would
 * not fit.
 */
bool voice_speech_plan_add_number(voice_speech_plan_t *plan, long value);

/**
 * @brief Append a number that may have a fractional part.
 *
 * The whole part is spoken as above; anything after the point is spoken digit
 * by digit, the way people read decimals aloud — "three point one four", not
 * "three and fourteen hundredths".
 */
bool voice_speech_plan_add_decimal(voice_speech_plan_t *plan, double value);

/** @brief True when the plan holds a complete, sayable sentence. */
bool voice_speech_plan_is_usable(const voice_speech_plan_t *plan);

/* --- The fragments ------------------------------------------------------ */

/*
 * Named rather than written inline at the call sites, so that the compiler
 * catches a typo that would otherwise become a silent missing recording.
 */
extern const char *const VOICE_FRAGMENT_IT_IS;
extern const char *const VOICE_FRAGMENT_OH;
extern const char *const VOICE_FRAGMENT_OCLOCK;
extern const char *const VOICE_FRAGMENT_MORNING;
extern const char *const VOICE_FRAGMENT_AFTERNOON;
extern const char *const VOICE_FRAGMENT_EVENING;

extern const char *const VOICE_FRAGMENT_TODAY_IS;
extern const char *const VOICE_FRAGMENT_THE;

extern const char *const VOICE_FRAGMENT_MINUS;
extern const char *const VOICE_FRAGMENT_DEGREES_F;
extern const char *const VOICE_FRAGMENT_DEGREES_C;
extern const char *const VOICE_FRAGMENT_IN_HERE;

extern const char *const VOICE_FRAGMENT_HUMIDITY_IS;
extern const char *const VOICE_FRAGMENT_PERCENT;

extern const char *const VOICE_FRAGMENT_VOLUME_UP_TO;
extern const char *const VOICE_FRAGMENT_VOLUME_DOWN_TO;

extern const char *const VOICE_FRAGMENT_ABOUT;
extern const char *const VOICE_FRAGMENT_MINUTE_LEFT;
extern const char *const VOICE_FRAGMENT_MINUTES_LEFT;
extern const char *const VOICE_FRAGMENT_SECOND_LEFT;
extern const char *const VOICE_FRAGMENT_SECONDS_LEFT;
extern const char *const VOICE_FRAGMENT_MINUTES_AND;

extern const char *const VOICE_FRAGMENT_THATS;
extern const char *const VOICE_FRAGMENT_HUNDRED;
extern const char *const VOICE_FRAGMENT_THOUSAND;
extern const char *const VOICE_FRAGMENT_POINT;
extern const char *const VOICE_FRAGMENT_THE_ANSWER_IS;

extern const char *const VOICE_FRAGMENT_AWAKE_FOR;
extern const char *const VOICE_FRAGMENT_MINUTE;
extern const char *const VOICE_FRAGMENT_MINUTES;
extern const char *const VOICE_FRAGMENT_HOUR;
extern const char *const VOICE_FRAGMENT_HOURS;
extern const char *const VOICE_FRAGMENT_AND;
extern const char *const VOICE_FRAGMENT_MEMORY_FREE;
extern const char *const VOICE_FRAGMENT_KB_INTERNAL;
extern const char *const VOICE_FRAGMENT_ONLINE_AS;
extern const char *const VOICE_FRAGMENT_OFFLINE_NOW;

/**
 * @brief A weekday name, Sunday first, or NULL when @p index is out of range.
 *
 * Indexed the way struct tm counts: tm_wday, 0 = Sunday.
 */
const char *voice_speech_weekday(int index);

/**
 * @brief A month name, January first, or NULL when @p index is out of range.
 *
 * Indexed the way struct tm counts: tm_mon, 0 = January.
 */
const char *voice_speech_month(int index);

/* --- What the clip generator and the tests need -------------------------- */

/**
 * @brief Every fragment in this module, for checking recordings exist.
 *
 * @param count  Receives how many there are.
 * @return The array. Owned here, valid forever.
 */
const char *const *voice_speech_all_fragments(size_t *count);

#ifdef __cplusplus
}
#endif
