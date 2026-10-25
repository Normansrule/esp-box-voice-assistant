/*
 * voice_box_context.c — The "[box: ...]" line sent with a Home AI question.
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 *
 * See voice_box_context.h for why it exists and what goes in it.
 */

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "voice_box_context.h"

/** Append to a fixed buffer; once it is full, stop and remember that. */
typedef struct {
    char  *out;
    size_t size;
    size_t length;
    bool   overflowed;
    int    parts;
} line_t;

static void append(line_t *line, const char *format, ...)
{
    if (line->overflowed) {
        return;
    }

    va_list args;
    va_start(args, format);
    const int wrote = vsnprintf(line->out + line->length, line->size - line->length,
                                format, args);
    va_end(args);

    if (wrote < 0 || (size_t)wrote >= line->size - line->length) {
        line->overflowed = true;
        return;
    }
    line->length += (size_t)wrote;
}

/** Start the next part: the opening on the first, a separator after that. */
static void next_part(line_t *line)
{
    append(line, (line->parts == 0) ? "[box: " : "; ");
    line->parts++;
}

size_t voice_box_context_format(const voice_box_context_t *context, char *out, size_t out_size)
{
    if (out == NULL || out_size == 0) {
        return 0;
    }
    out[0] = '\0';
    if (context == NULL) {
        return 0;
    }

    line_t line = { .out = out, .size = out_size };

    if (context->have_time) {
        char when[32];
        /* "Sat 24 Oct 17:20": the weekday matters as much as the clock. */
        if (strftime(when, sizeof(when), "%a %d %b %H:%M", &context->local_time) > 0) {
            next_part(&line);
            append(&line, "time %s", when);
        }
    }

    if (context->have_room) {
        next_part(&line);
        append(&line, "room %.1f %s; humidity %.0f%%", context->temperature,
               context->fahrenheit ? "F" : "C", context->humidity_percent);
    }

    if (context->have_battery) {
        next_part(&line);
        append(&line, "battery about %u%% (estimate%s)",
               (unsigned)context->battery_percent_estimate,
               context->battery_low ? ", low" : "");
    }

    if (context->have_presence) {
        next_part(&line);
        append(&line, "%s", context->someone_nearby ? "someone nearby" : "nobody nearby");
    }

    if (line.parts == 0) {
        return 0;
    }
    append(&line, "]");

    /*
     * All or nothing. A line cut off halfway through a number is worse than no
     * line, because the model would read the truncated figure as a reading.
     */
    if (line.overflowed) {
        out[0] = '\0';
        return 0;
    }
    return line.length;
}
