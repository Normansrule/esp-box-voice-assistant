/*
 * voice_box_context.h — What the box can tell the home brain about right now.
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 *
 * A language model on a PC knows nothing about the room the box is sitting in.
 * So in Home AI mode each question carries one extra line of real readings:
 *
 *     [box: time Sat 24 Oct 17:20; room 22.5 C; humidity 41%;
 *      battery about 80% (estimate); someone nearby]
 *
 * which lets "is it warm in here?", "how is your battery?" and "should I open
 * a window?" get answers from the box's own sensors instead of a guess. The
 * home brain moves the line out of the question and into its instructions, so
 * the model uses it without reading it back.
 *
 * Only readings the box actually has go in. A missing dock, an unset clock or
 * an unavailable battery simply leaves that part out; nothing is ever filled in
 * with a placeholder number. The battery figure is labelled an estimate
 * because it is one (see hw_battery.h).
 *
 * Sent to the home brain only, never to OpenAI: that is a third party, and the
 * owner did not agree to send it their room's readings.
 *
 * Pure formatting, so the host tests can check it. See tests/test_desk_buddy.c.
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Everything that may go in the line. Each part has its own "have it" flag. */
typedef struct {
    bool      have_time;
    struct tm local_time;

    bool      have_room;
    float     temperature;        /*!< In the owner's chosen unit            */
    bool      fahrenheit;         /*!< Which unit that is                    */
    float     humidity_percent;

    bool      have_battery;
    uint8_t   battery_percent_estimate;
    bool      battery_low;

    bool      have_presence;
    bool      someone_nearby;
} voice_box_context_t;

/**
 * @brief Write the "[box: ...]" line for @p context into @p out.
 *
 * @return The length written, or 0 (with out[0] = '\0') when there is nothing
 *         worth sending or no room for it. Never overflows @p out.
 */
size_t voice_box_context_format(const voice_box_context_t *context, char *out, size_t out_size);

#ifdef __cplusplus
}
#endif
