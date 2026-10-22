/*
 * voice_number_words.h — Numbers, written the way they are said.
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 *
 * WHY A TABLE OF WORDS
 * --------------------
 * The buddy speaks offline by looking up a recording of the exact sentence it
 * is about to show. That works beautifully for anything it knows by heart and
 * not at all for anything with a number in it — there is no recording of "it is
 * 3:47 in the afternoon", and there never can be, because there are eighty-six
 * thousand of those.
 *
 * So a spoken answer with a number in it is assembled from pieces instead: a
 * clip per fragment, a clip per number, played back to back. This file is the
 * numbers half of that. Every string here gets its own recording at build time,
 * which is why they are spelled out as words rather than produced by a
 * printf — the text IS the lookup key.
 *
 * The range stops at 120 because that covers everything the buddy actually
 * says: minutes past the hour, days of the month, a percentage, and a
 * temperature in either scale. Anything outside it has no clip and is shown on
 * screen without being spoken, which is the same graceful failure a reply with
 * no recording has always had.
 */

#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Highest number with a recording. See the note above for why it stops here. */
#define VOICE_NUMBER_WORDS_MAX      120

/**
 * @brief The words for a number, or NULL if it has no recording.
 *
 * @param value  The number to spell out.
 * @return A string such as "forty seven", owned by this module and valid
 *         forever, or NULL when @p value is outside 0..VOICE_NUMBER_WORDS_MAX.
 */
const char *voice_number_words(int value);

/**
 * @brief How many numbers are in the table. Used by the host tests.
 */
size_t voice_number_words_count(void);

#ifdef __cplusplus
}
#endif
