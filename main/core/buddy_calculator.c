/*
 * buddy_calculator.c — Arithmetic the box can do with nothing connected.
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 *
 * Pure state machine. See buddy_calculator.h for what it is for and where the
 * deliberate limits are.
 */

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "buddy_calculator.h"

/** Most digits that may be typed, so the display cannot be overrun. */
#define MAX_TYPED_DIGITS        12

/* ------------------------------------------------------------------------ */
/* Formatting                                                                */
/* ------------------------------------------------------------------------ */

void buddy_calculator_format(double value, char *out, size_t out_size)
{
    if (out == NULL || out_size == 0) {
        return;
    }

    /*
     * Not a number, or past what a double can represent exactly. Rather than
     * printing "inf" or "nan" at somebody, say the thing that is actually
     * true — the sum went somewhere this calculator cannot follow.
     */
    if (isnan(value) || isinf(value)) {
        snprintf(out, out_size, "Too big");
        return;
    }

    /*
     * Whole numbers print as whole numbers. "%g" would give "1e+06" for a
     * million, which is correct and unreadable on a desk toy.
     */
    if (value == floor(value) && fabs(value) < 1e12) {
        snprintf(out, out_size, "%.0f", value);
        return;
    }

    snprintf(out, out_size, "%.*f", BUDDY_CALC_DECIMAL_PLACES, value);

    /*
     * Trim the zeros the fixed format leaves behind: 2.5 should read "2.5",
     * not "2.500000". The decimal point goes too if nothing survives after it.
     */
    char *end = out + strlen(out) - 1;
    while (end > out && *end == '0') {
        *end-- = '\0';
    }
    if (end > out && *end == '.') {
        *end = '\0';
    }
}

/* ------------------------------------------------------------------------ */
/* Reading the display back as a number                                      */
/* ------------------------------------------------------------------------ */

/**
 * @brief Parse the display without using the C library's locale-aware atof.
 *
 * strtod respects the decimal separator of the current locale, which on a
 * European locale is a comma — and this display always uses a point, because
 * that is the key on the keypad. Doing it by hand removes the dependency
 * entirely and makes the host tests locale-proof.
 */
static double display_to_value(const char *text)
{
    double sign = 1.0;
    double whole = 0.0;
    double fraction = 0.0;
    double scale = 1.0;
    bool after_point = false;

    if (*text == '-') {
        sign = -1.0;
        text++;
    }

    for (; *text != '\0'; text++) {
        if (*text == '.') {
            after_point = true;
            continue;
        }
        if (*text < '0' || *text > '9') {
            break;
        }
        if (after_point) {
            scale *= 10.0;
            fraction += (double)(*text - '0') / scale;
        } else {
            whole = whole * 10.0 + (double)(*text - '0');
        }
    }

    return sign * (whole + fraction);
}

/* ------------------------------------------------------------------------ */
/* Keys                                                                      */
/* ------------------------------------------------------------------------ */

void buddy_calculator_reset(buddy_calculator_t *calc)
{
    if (calc == NULL) {
        return;
    }
    memset(calc, 0, sizeof(*calc));
    snprintf(calc->display, sizeof(calc->display), "0");
}

/** @brief How many digit characters are currently on the display. */
static size_t digits_typed(const buddy_calculator_t *calc)
{
    size_t count = 0;
    for (const char *c = calc->display; *c != '\0'; c++) {
        if (*c >= '0' && *c <= '9') {
            count++;
        }
    }
    return count;
}

static void start_new_entry(buddy_calculator_t *calc, char first)
{
    calc->display[0] = first;
    calc->display[1] = '\0';
    calc->showing_result = false;
}

static bool press_digit(buddy_calculator_t *calc, char digit)
{
    if (calc->showing_result || calc->error) {
        calc->error = false;
        start_new_entry(calc, digit);
        return true;
    }

    /* A leading zero is replaced rather than appended to: "0" then 5 is "5". */
    if (strcmp(calc->display, "0") == 0) {
        start_new_entry(calc, digit);
        return true;
    }
    if (strcmp(calc->display, "-0") == 0) {
        calc->display[1] = digit;
        calc->display[2] = '\0';
        return true;
    }

    if (digits_typed(calc) >= MAX_TYPED_DIGITS) {
        return false;
    }

    const size_t length = strlen(calc->display);
    if (length + 2 > sizeof(calc->display)) {
        return false;
    }

    calc->display[length] = digit;
    calc->display[length + 1] = '\0';
    return true;
}

static bool press_point(buddy_calculator_t *calc)
{
    if (calc->showing_result || calc->error) {
        calc->error = false;
        start_new_entry(calc, '0');
    }

    /* One point per number, and it is a no-op rather than an error. */
    if (strchr(calc->display, '.') != NULL) {
        return false;
    }

    const size_t length = strlen(calc->display);
    if (length + 2 > sizeof(calc->display)) {
        return false;
    }

    calc->display[length] = '.';
    calc->display[length + 1] = '\0';
    calc->showing_result = false;
    return true;
}

static bool press_sign(buddy_calculator_t *calc)
{
    if (calc->error) {
        return false;
    }

    /* Nothing is gained by writing "-0", so leave a bare zero alone. */
    if (strcmp(calc->display, "0") == 0) {
        return false;
    }

    if (calc->display[0] == '-') {
        memmove(calc->display, calc->display + 1, strlen(calc->display));
        return true;
    }

    const size_t length = strlen(calc->display);
    if (length + 2 > sizeof(calc->display)) {
        return false;
    }

    memmove(calc->display + 1, calc->display, length + 1);
    calc->display[0] = '-';
    return true;
}

static bool press_backspace(buddy_calculator_t *calc)
{
    if (calc->error || calc->showing_result) {
        /*
         * Backspacing an answer clears rather than editing it. Editing a
         * result digit by digit produces a number that came from nowhere.
         */
        buddy_calculator_reset(calc);
        return true;
    }

    size_t length = strlen(calc->display);
    if (length <= 1 || (length == 2 && calc->display[0] == '-')) {
        start_new_entry(calc, '0');
        return true;
    }

    calc->display[length - 1] = '\0';
    return true;
}

/**
 * @brief Apply the pending operation, leaving the answer on the display.
 *
 * @return false when the sum could not be done, with the display explaining.
 */
static bool apply_pending(buddy_calculator_t *calc, double right)
{
    double result = right;

    switch (calc->pending_operator) {
    case BUDDY_CALC_KEY_ADD:      result = calc->accumulator + right; break;
    case BUDDY_CALC_KEY_SUBTRACT: result = calc->accumulator - right; break;
    case BUDDY_CALC_KEY_MULTIPLY: result = calc->accumulator * right; break;

    case BUDDY_CALC_KEY_DIVIDE:
        if (right == 0.0) {
            /*
             * Said in words rather than shown as "inf" or "nan". Dividing by
             * zero is the one arithmetic mistake everybody makes on purpose at
             * least once, and it deserves an answer rather than a symbol.
             */
            calc->error = true;
            snprintf(calc->display, sizeof(calc->display), "Cannot divide by 0");
            calc->pending_operator = 0;
            calc->showing_result = false;
            return false;
        }
        result = calc->accumulator / right;
        break;

    case BUDDY_CALC_KEY_PERCENT:
        /*
         * "50 % 10" reads as "ten percent of fifty", which is what the key
         * means on a pocket calculator and is not the same as the modulo this
         * character means in C.
         */
        result = calc->accumulator * right / 100.0;
        break;

    default:
        break;
    }

    if (isnan(result) || isinf(result)) {
        calc->error = true;
        snprintf(calc->display, sizeof(calc->display), "Too big");
        calc->pending_operator = 0;
        return false;
    }

    calc->accumulator = result;
    buddy_calculator_format(result, calc->display, sizeof(calc->display));
    return true;
}

static bool press_operator(buddy_calculator_t *calc, char op)
{
    if (calc->error) {
        return false;
    }

    const double current = display_to_value(calc->display);

    /*
     * Pressing an operator when one is already pending finishes the sum so
     * far, so 2 + 3 + 4 accumulates rather than forgetting the first half.
     * Pressing one straight after another just changes the operator, because
     * that is always a correction rather than an intention.
     */
    if (calc->pending_operator != 0 && !calc->showing_result) {
        if (!apply_pending(calc, current)) {
            return true;   /* The error is on the display. */
        }
    } else {
        calc->accumulator = current;
    }

    calc->pending_operator = op;
    calc->showing_result = true;   /* The next digit starts a new number. */
    return true;
}

static bool press_equals(buddy_calculator_t *calc)
{
    if (calc->error) {
        return false;
    }
    if (calc->pending_operator == 0) {
        /* Nothing pending: equals confirms what is there and says it aloud. */
        calc->accumulator = display_to_value(calc->display);
        calc->showing_result = true;
        return true;
    }

    const double right = display_to_value(calc->display);
    apply_pending(calc, right);

    calc->pending_operator = 0;
    calc->showing_result = true;
    return true;
}

bool buddy_calculator_press(buddy_calculator_t *calc, char key)
{
    if (calc == NULL) {
        return false;
    }

    /* An untouched calculator is a cleared one, so pressing anything works. */
    if (calc->display[0] == '\0') {
        buddy_calculator_reset(calc);
    }

    if (key >= '0' && key <= '9') {
        return press_digit(calc, key);
    }

    switch (key) {
    case BUDDY_CALC_KEY_POINT:      return press_point(calc);
    case BUDDY_CALC_KEY_SIGN:       return press_sign(calc);
    case BUDDY_CALC_KEY_BACKSPACE:  return press_backspace(calc);
    case BUDDY_CALC_KEY_EQUALS:     return press_equals(calc);

    case BUDDY_CALC_KEY_CLEAR:
        buddy_calculator_reset(calc);
        return true;

    case BUDDY_CALC_KEY_ADD:
    case BUDDY_CALC_KEY_SUBTRACT:
    case BUDDY_CALC_KEY_MULTIPLY:
    case BUDDY_CALC_KEY_DIVIDE:
    case BUDDY_CALC_KEY_PERCENT:
        return press_operator(calc, key);

    default:
        return false;
    }
}

const char *buddy_calculator_display(const buddy_calculator_t *calc)
{
    if (calc == NULL || calc->display[0] == '\0') {
        return "0";
    }
    return calc->display;
}

double buddy_calculator_value(const buddy_calculator_t *calc)
{
    if (calc == NULL || calc->error) {
        return 0.0;
    }
    return display_to_value(calc->display);
}

bool buddy_calculator_has_spoken_answer(const buddy_calculator_t *calc)
{
    return (calc != NULL) && calc->showing_result && !calc->error &&
           (calc->pending_operator == 0);
}
