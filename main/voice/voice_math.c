/*
 * voice_math.c — A very small arithmetic parser.
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 *
 * HOW IT WORKS
 * ------------
 * Three passes, each one boring on purpose:
 *
 *   1. NORMALISE  Lowercase the question and throw away anything that is not a
 *                 letter, a digit or one of the symbols we care about. The
 *                 symbols get spaces padded around them so that "2+2" and
 *                 "2 + 2" end up looking identical.
 *
 *   2. TOKENISE   Split on spaces into a fixed-size array. Fixed size means no
 *                 allocation and no way to run away with memory on a strange
 *                 input.
 *
 *   3. PARSE      Find a number, then an operator, then a second number.
 *                 Everything before the first number ("what is", "hey buddy,
 *                 tell me") is skipped, because people rarely just say a sum.
 *
 * The result is computed as a fraction — numerator over denominator — so that
 * one formatting routine can handle every operator, including the two that can
 * produce something that is not a whole number.
 */

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "voice_math.h"

/* ------------------------------------------------------------------------ */
/* Limits                                                                    */
/* ------------------------------------------------------------------------ */

/** Longest question we will look at. Anything longer is not a sum. */
#define MAX_QUESTION_LEN    192

/** Most tokens we will consider, and the longest single token. */
#define MAX_TOKENS          32
#define MAX_TOKEN_LEN       16

/**
 * Largest operand we accept.
 *
 * Not a technical limit — the arithmetic below is done in 64 bits and would
 * cope with far more. It is a judgement call: past a million the answer stops
 * being something a desk toy saying it out loud is any use for, and refusing
 * politely is better than reciting fourteen digits.
 */
#define MAX_OPERAND         1000000LL

/* ------------------------------------------------------------------------ */
/* Number words                                                              */
/* ------------------------------------------------------------------------ */

/** How a number word combines with the ones before it. */
typedef enum {
    WORD_PLAIN,        /*!< Adds to the number being built ("twenty", "seven") */
    WORD_HUNDRED,      /*!< Multiplies it by a hundred                         */
    WORD_THOUSAND,     /*!< Multiplies it by a thousand and banks the result   */
} number_word_kind_t;

typedef struct {
    const char        *word;
    long long          value;
    number_word_kind_t kind;
} number_word_t;

static const number_word_t k_number_words[] = {
    { "zero",       0, WORD_PLAIN },  { "oh",         0, WORD_PLAIN },
    { "one",        1, WORD_PLAIN },  { "two",        2, WORD_PLAIN },
    { "three",      3, WORD_PLAIN },  { "four",       4, WORD_PLAIN },
    { "five",       5, WORD_PLAIN },  { "six",        6, WORD_PLAIN },
    { "seven",      7, WORD_PLAIN },  { "eight",      8, WORD_PLAIN },
    { "nine",       9, WORD_PLAIN },  { "ten",       10, WORD_PLAIN },
    { "eleven",    11, WORD_PLAIN },  { "twelve",    12, WORD_PLAIN },
    { "thirteen",  13, WORD_PLAIN },  { "fourteen",  14, WORD_PLAIN },
    { "fifteen",   15, WORD_PLAIN },  { "sixteen",   16, WORD_PLAIN },
    { "seventeen", 17, WORD_PLAIN },  { "eighteen",  18, WORD_PLAIN },
    { "nineteen",  19, WORD_PLAIN },  { "twenty",    20, WORD_PLAIN },
    { "thirty",    30, WORD_PLAIN },  { "forty",     40, WORD_PLAIN },
    { "fourty",    40, WORD_PLAIN },  /* common misspelling from transcription */
    { "fifty",     50, WORD_PLAIN },  { "sixty",     60, WORD_PLAIN },
    { "seventy",   70, WORD_PLAIN },  { "eighty",    80, WORD_PLAIN },
    { "ninety",    90, WORD_PLAIN },
    { "hundred",  100, WORD_HUNDRED },
    { "thousand", 1000, WORD_THOUSAND },
};

static const number_word_t *lookup_number_word(const char *token)
{
    const size_t count = sizeof(k_number_words) / sizeof(k_number_words[0]);
    for (size_t i = 0; i < count; i++) {
        if (strcmp(token, k_number_words[i].word) == 0) {
            return &k_number_words[i];
        }
    }
    return NULL;
}

/* ------------------------------------------------------------------------ */
/* Operators                                                                 */
/* ------------------------------------------------------------------------ */

typedef enum {
    OP_ADD,
    OP_SUBTRACT,
    OP_MULTIPLY,
    OP_DIVIDE,
    OP_PERCENT_OF,
} operator_t;

/* ------------------------------------------------------------------------ */
/* Pass one and two: normalise and tokenise                                  */
/* ------------------------------------------------------------------------ */

static bool token_is_all_digits(const char *token)
{
    if (token[0] == '\0') {
        return false;
    }
    for (const char *c = token; *c != '\0'; c++) {
        if (!isdigit((unsigned char)*c)) {
            return false;
        }
    }
    return true;
}

/**
 * @brief Lowercase the question and split it into tokens.
 *
 * Symbols get a space on each side so "2+2" tokenises the same as "2 + 2".
 * Apostrophes are dropped rather than replaced, so "what's" becomes "whats"
 * instead of two useless fragments.
 *
 * @return Number of tokens produced.
 */
static int tokenise(const char *question, char tokens[MAX_TOKENS][MAX_TOKEN_LEN])
{
    char flat[MAX_QUESTION_LEN * 3];   /* Worst case: every char becomes " c " */
    size_t out = 0;

    for (size_t in = 0; question[in] != '\0' && in < MAX_QUESTION_LEN; in++) {
        const unsigned char c = (unsigned char)question[in];

        if (isalnum(c)) {
            if (out + 1 >= sizeof(flat)) {
                break;
            }
            flat[out++] = (char)tolower(c);
        } else if (c == '+' || c == '-' || c == '*' || c == '/') {
            if (out + 3 >= sizeof(flat)) {
                break;
            }
            flat[out++] = ' ';
            flat[out++] = (char)c;
            flat[out++] = ' ';
        } else if (c == '\'') {
            /* Swallow it: "what's" -> "whats". */
        } else {
            if (out + 1 >= sizeof(flat)) {
                break;
            }
            flat[out++] = ' ';
        }
    }
    flat[out] = '\0';

    int count = 0;
    const char *cursor = flat;

    while (*cursor != '\0' && count < MAX_TOKENS) {
        while (*cursor == ' ') {
            cursor++;
        }
        if (*cursor == '\0') {
            break;
        }

        size_t length = 0;
        while (cursor[length] != '\0' && cursor[length] != ' ') {
            length++;
        }

        /* Tokens longer than the buffer are truncated, never overflowed. */
        const size_t copy = (length < MAX_TOKEN_LEN - 1) ? length : MAX_TOKEN_LEN - 1;
        memcpy(tokens[count], cursor, copy);
        tokens[count][copy] = '\0';
        count++;

        cursor += length;
    }

    return count;
}

/* ------------------------------------------------------------------------ */
/* Pass three: parse                                                         */
/* ------------------------------------------------------------------------ */

/**
 * @brief Read one number, in digits or in words, starting at *position.
 *
 * Handles the way English builds numbers out of several words: "twenty one",
 * "one hundred", "two hundred and five", "three thousand".
 *
 * @return true when a number was read; *position then points past it.
 */
static bool parse_number(char tokens[MAX_TOKENS][MAX_TOKEN_LEN], int count,
                         int *position, long long *out)
{
    long long banked = 0;      /* Completed thousands                         */
    long long current = 0;     /* The part being assembled right now          */
    bool got_something = false;

    while (*position < count) {
        const char *token = tokens[*position];

        if (token_is_all_digits(token)) {
            /*
             * A digit run is a complete number by itself. Stopping here means
             * "5 5" reads as two numbers rather than silently becoming ten.
             */
            if (got_something) {
                break;
            }
            current = atoll(token);
            got_something = true;
            (*position)++;
            continue;
        }

        const number_word_t *word = lookup_number_word(token);

        if (word == NULL) {
            /*
             * "and" is part of the number in "one hundred and five", but it is
             * a word boundary anywhere else — so only step over it when a
             * number word genuinely follows.
             */
            if (got_something && strcmp(token, "and") == 0 &&
                (*position + 1) < count &&
                (lookup_number_word(tokens[*position + 1]) != NULL ||
                 token_is_all_digits(tokens[*position + 1]))) {
                (*position)++;
                continue;
            }
            break;
        }

        switch (word->kind) {
        case WORD_HUNDRED:
            current = ((current == 0) ? 1 : current) * 100;
            break;
        case WORD_THOUSAND:
            banked += ((current == 0) ? 1 : current) * 1000;
            current = 0;
            break;
        case WORD_PLAIN:
        default:
            current += word->value;
            break;
        }

        got_something = true;
        (*position)++;
    }

    if (!got_something) {
        return false;
    }

    *out = banked + current;
    return true;
}

/**
 * @brief Read one operator, stepping over the filler words that follow it.
 *
 * "multiplied by", "divided by" and "percent of" are two tokens each; the
 * second is consumed here so the caller can go straight on to the next number.
 *
 * @return true when an operator was read; *position then points past it.
 */
static bool parse_operator(char tokens[MAX_TOKENS][MAX_TOKEN_LEN], int count,
                           int *position, operator_t *out)
{
    if (*position >= count) {
        return false;
    }

    const char *token = tokens[*position];
    const char *next = ((*position + 1) < count) ? tokens[*position + 1] : "";

    struct { const char *word; const char *follower; operator_t op; } table[] = {
        { "plus",       NULL,   OP_ADD },
        { "+",          NULL,   OP_ADD },
        { "add",        NULL,   OP_ADD },
        { "added",      "to",   OP_ADD },
        { "minus",      NULL,   OP_SUBTRACT },
        { "-",          NULL,   OP_SUBTRACT },
        { "subtract",   NULL,   OP_SUBTRACT },
        { "less",       NULL,   OP_SUBTRACT },
        { "take",       "away", OP_SUBTRACT },
        { "times",      NULL,   OP_MULTIPLY },
        { "*",          NULL,   OP_MULTIPLY },
        { "x",          NULL,   OP_MULTIPLY },
        { "multiplied", "by",   OP_MULTIPLY },
        { "divided",    "by",   OP_DIVIDE },
        { "/",          NULL,   OP_DIVIDE },
        { "over",       NULL,   OP_DIVIDE },
        { "percent",    "of",   OP_PERCENT_OF },
    };

    const size_t entries = sizeof(table) / sizeof(table[0]);

    for (size_t i = 0; i < entries; i++) {
        if (strcmp(token, table[i].word) != 0) {
            continue;
        }

        *out = table[i].op;
        (*position)++;

        /* Step over the optional second word, when it is actually there. */
        if (table[i].follower != NULL && strcmp(next, table[i].follower) == 0) {
            (*position)++;
        }
        return true;
    }

    return false;
}

/* ------------------------------------------------------------------------ */
/* Formatting                                                                */
/* ------------------------------------------------------------------------ */

/**
 * @brief Render numerator/denominator as the shortest sensible decimal.
 *
 * Whole numbers print as whole numbers. Everything else gets two decimal
 * places, rounded, with a trailing zero trimmed — so a half is "2.5" and a
 * third is "0.33", and neither reads like a computer talking.
 */
static bool format_result(long long numerator, long long denominator,
                          char *answer, size_t answer_size)
{
    if (denominator == 0) {
        return false;
    }

    if (numerator % denominator == 0) {
        const int written = snprintf(answer, answer_size, "%lld", numerator / denominator);
        return written > 0 && (size_t)written < answer_size;
    }

    const bool negative = ((numerator < 0) != (denominator < 0));
    long long top = numerator < 0 ? -numerator : numerator;
    const long long bottom = denominator < 0 ? -denominator : denominator;

    /* Two decimal places, rounded half-up, using integers only. */
    const long long scaled = (top * 100 + bottom / 2) / bottom;

    const long long whole = scaled / 100;
    const long long fraction = scaled % 100;

    int written;
    if (fraction == 0) {
        written = snprintf(answer, answer_size, "%s%lld", negative ? "-" : "", whole);
    } else if (fraction % 10 == 0) {
        written = snprintf(answer, answer_size, "%s%lld.%lld",
                           negative ? "-" : "", whole, fraction / 10);
    } else {
        written = snprintf(answer, answer_size, "%s%lld.%02lld",
                           negative ? "-" : "", whole, fraction);
    }

    return written > 0 && (size_t)written < answer_size;
}

/* ------------------------------------------------------------------------ */
/* The one public entry point                                                */
/* ------------------------------------------------------------------------ */

bool voice_math_try_answer(const char *question, char *answer, size_t answer_size)
{
    if (question == NULL || answer == NULL || answer_size < 4) {
        return false;
    }

    char tokens[MAX_TOKENS][MAX_TOKEN_LEN];
    const int count = tokenise(question, tokens);
    if (count < 3) {
        return false;   /* Not enough words to be "<number> <op> <number>". */
    }

    /*
     * Skip whatever the question opens with — "what is", "hey buddy tell me",
     * "quick question" — by hunting for the first position where a complete
     * sum starts. Trying every start costs nothing at this size and means the
     * parser is not fussy about how the question is phrased.
     */
    for (int start = 0; start < count - 2; start++) {
        int position = start;

        long long left = 0;
        if (!parse_number(tokens, count, &position, &left)) {
            continue;
        }

        operator_t op;
        if (!parse_operator(tokens, count, &position, &op)) {
            continue;
        }

        long long right = 0;
        if (!parse_number(tokens, count, &position, &right)) {
            continue;
        }

        if (left > MAX_OPERAND || right > MAX_OPERAND) {
            return false;   /* Understood, but not worth saying out loud. */
        }

        long long numerator;
        long long denominator = 1;

        switch (op) {
        case OP_ADD:        numerator = left + right;                   break;
        case OP_SUBTRACT:   numerator = left - right;                   break;
        case OP_MULTIPLY:   numerator = left * right;                   break;
        case OP_DIVIDE:     numerator = left; denominator = right;      break;
        case OP_PERCENT_OF: numerator = left * right; denominator = 100; break;
        default:            return false;
        }

        /* Dividing by zero has no answer worth inventing one for. */
        if (denominator == 0) {
            return false;
        }

        return format_result(numerator, denominator, answer, answer_size);
    }

    return false;
}
