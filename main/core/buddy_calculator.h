/*
 * buddy_calculator.h — Arithmetic the box can do with nothing connected.
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 *
 * WHY A KEYPAD AND NOT JUST VOICE
 * -------------------------------
 * The buddy already answers spoken sums, but only the eight registered in
 * voice_commands.c — MultiNet matches whole phrases it was given in advance, so
 * "what is two plus two" works and "what is four hundred and seven times
 * nineteen" cannot, ever, offline. There is no arrangement of a phrase list
 * that covers arbitrary arithmetic; the combinations run to millions.
 *
 * So the answer to "let me ask any calculation offline" is a keypad. You tap
 * the sum, the box works it out on the chip, and it SAYS the answer out loud
 * using the same recorded number words the clock and thermometer use. No
 * network, no account, no per-question cost.
 *
 * WHAT THIS FILE IS
 * -----------------
 * The engine only: keys in, display text out. It has no idea a screen exists,
 * which is what lets the host tests hammer every sequence of button presses
 * including the ones nobody would type on purpose. ui_calculator.c draws it.
 *
 * DELIBERATE LIMITS
 * -----------------
 * One operation at a time, left to right, the way a pocket calculator works
 * rather than the way algebra does: 2 + 3 x 4 gives 20, not 14. That is not an
 * oversight. A four-function calculator with no precedence is a thing people
 * already know how to use, and adding precedence without brackets to show it
 * would make the display lie about what is pending.
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Longest display string, including the terminator. */
#define BUDDY_CALC_DISPLAY_MAX_LEN      24

/** Digits kept after the decimal point in a result. */
#define BUDDY_CALC_DECIMAL_PLACES       6

/**
 * The keys. Passed to buddy_calculator_press() as plain characters so a test
 * can type a whole sum as a string.
 */
#define BUDDY_CALC_KEY_ADD              '+'
#define BUDDY_CALC_KEY_SUBTRACT         '-'
#define BUDDY_CALC_KEY_MULTIPLY         '*'
#define BUDDY_CALC_KEY_DIVIDE           '/'
#define BUDDY_CALC_KEY_PERCENT          '%'
#define BUDDY_CALC_KEY_EQUALS           '='
#define BUDDY_CALC_KEY_POINT            '.'
#define BUDDY_CALC_KEY_CLEAR            'C'
#define BUDDY_CALC_KEY_BACKSPACE        '<'
#define BUDDY_CALC_KEY_SIGN             '~'   /*!< Flip between plus and minus */

/** Everything the calculator remembers. Zero-initialise, or reset it. */
typedef struct {
    /** What is on the screen right now, always valid and NUL-terminated. */
    char   display[BUDDY_CALC_DISPLAY_MAX_LEN];

    /** The number waiting on the left of a pending operation. */
    double accumulator;

    /** The pending operation, or 0 when there is none. */
    char   pending_operator;

    /** True while the display is a finished answer rather than typing. */
    bool   showing_result;

    /** True when the last operation could not be done (dividing by zero). */
    bool   error;
} buddy_calculator_t;

/** @brief Clear everything, as the C key does. */
void buddy_calculator_reset(buddy_calculator_t *calc);

/**
 * @brief Press one key.
 *
 * Every key is safe in every state — that is the point of testing this
 * separately. Pressing an operator twice replaces it, pressing equals with
 * nothing pending repeats the display, and an error is cleared by C alone.
 *
 * @param calc  The calculator.
 * @param key   One of the BUDDY_CALC_KEY_* values, or '0' to '9'.
 * @return true when the key changed anything.
 */
bool buddy_calculator_press(buddy_calculator_t *calc, char key);

/**
 * @brief The text to show, never NULL and never empty.
 */
const char *buddy_calculator_display(const buddy_calculator_t *calc);

/**
 * @brief The current value as a number.
 *
 * Used to speak the answer. Meaningless while @p calc reports an error.
 */
double buddy_calculator_value(const buddy_calculator_t *calc);

/**
 * @brief True when the display is a finished answer worth saying out loud.
 *
 * Typing "4", "2" should not have the buddy announcing "four" then
 * "forty two" — only pressing equals produces something worth speaking.
 */
bool buddy_calculator_has_spoken_answer(const buddy_calculator_t *calc);

/**
 * @brief Format a number the way the display does.
 *
 * Exposed because the same rules — trailing zeros trimmed, very large and very
 * small values given in a readable form rather than exponent notation — are
 * wanted by anything that reports a result.
 *
 * @param value        The number.
 * @param out          Destination.
 * @param out_size     Size of @p out.
 */
void buddy_calculator_format(double value, char *out, size_t out_size);

#ifdef __cplusplus
}
#endif
