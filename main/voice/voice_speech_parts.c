/*
 * voice_speech_parts.c — Speaking a sentence that has a number in it.
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 *
 * EVERY STRING LITERAL IN THIS FILE IS RECORDED AT BUILD TIME.
 *
 * scripts/make_voice_clips.py renders every literal here, unfiltered — unlike
 * the fixed replies, which have to look like finished sentences to be picked
 * up. That is why fragments such as "in the afternoon" can live here and be
 * spoken even though they are not sentences on their own.
 *
 * See voice_speech_parts.h for why the buddy says things in pieces at all.
 */

#include "voice_number_words.h"
#include "voice_speech_parts.h"

/* ------------------------------------------------------------------------ */
/* The fragments                                                             */
/* ------------------------------------------------------------------------ */

/*
 * Punctuation is deliberate and load-bearing. eSpeak reads a full stop as a
 * falling, finished intonation and no stop as a continuing one, so a fragment
 * that ends a sentence carries its full stop and one that does not, does not.
 * Without that, an assembled answer sounds like a list of words rather than a
 * sentence.
 */

const char *const VOICE_FRAGMENT_IT_IS      = "It is";
const char *const VOICE_FRAGMENT_OH         = "oh";
const char *const VOICE_FRAGMENT_OCLOCK     = "o'clock";
const char *const VOICE_FRAGMENT_MORNING    = "in the morning.";
const char *const VOICE_FRAGMENT_AFTERNOON  = "in the afternoon.";
const char *const VOICE_FRAGMENT_EVENING    = "in the evening.";

const char *const VOICE_FRAGMENT_TODAY_IS   = "Today is";
const char *const VOICE_FRAGMENT_THE        = "the";

const char *const VOICE_FRAGMENT_MINUS      = "minus";
const char *const VOICE_FRAGMENT_DEGREES_F  = "degrees Fahrenheit";
const char *const VOICE_FRAGMENT_DEGREES_C  = "degrees Celsius";
const char *const VOICE_FRAGMENT_IN_HERE    = "in here.";

const char *const VOICE_FRAGMENT_HUMIDITY_IS = "Humidity is";
const char *const VOICE_FRAGMENT_PERCENT     = "percent.";

const char *const VOICE_FRAGMENT_VOLUME_UP_TO   = "Volume up to";
const char *const VOICE_FRAGMENT_VOLUME_DOWN_TO = "Volume down to";

const char *const VOICE_FRAGMENT_ABOUT         = "About";
const char *const VOICE_FRAGMENT_MINUTE_LEFT   = "minute left.";
const char *const VOICE_FRAGMENT_MINUTES_LEFT  = "minutes left.";
const char *const VOICE_FRAGMENT_SECOND_LEFT   = "second left.";
const char *const VOICE_FRAGMENT_SECONDS_LEFT  = "seconds left.";
const char *const VOICE_FRAGMENT_MINUTES_AND   = "minutes and";

const char *const VOICE_FRAGMENT_THATS      = "That's";

/*
 * The two words that turn a bank of 121 recordings into every number up to a
 * million, and the one that lets it read a decimal.
 */
const char *const VOICE_FRAGMENT_HUNDRED    = "hundred";
const char *const VOICE_FRAGMENT_THOUSAND   = "thousand";
const char *const VOICE_FRAGMENT_POINT      = "point";
const char *const VOICE_FRAGMENT_THE_ANSWER_IS = "The answer is";

/* Answers about itself, added in v3.5. */
const char *const VOICE_FRAGMENT_AWAKE_FOR    = "I have been awake for";
const char *const VOICE_FRAGMENT_MINUTE       = "minute.";
const char *const VOICE_FRAGMENT_MINUTES      = "minutes.";
const char *const VOICE_FRAGMENT_HOUR         = "hour.";
const char *const VOICE_FRAGMENT_HOURS        = "hours.";
const char *const VOICE_FRAGMENT_AND          = "and";
const char *const VOICE_FRAGMENT_MEMORY_FREE  = "I have";
const char *const VOICE_FRAGMENT_KB_INTERNAL  = "kilobytes of fast memory free.";
const char *const VOICE_FRAGMENT_ONLINE_AS    = "I am online. My address ends in";
const char *const VOICE_FRAGMENT_OFFLINE_NOW  = "I am not on Wi-Fi at the moment.";

static const char *const s_weekdays[] = {
    "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday",
};

static const char *const s_months[] = {
    "January", "February", "March", "April", "May", "June",
    "July", "August", "September", "October", "November", "December",
};

/**
 * Every fragment above, gathered in one place.
 *
 * Used by the host tests to check that nothing is named in the header without
 * being defined here, and available to anything else that needs to know the
 * full set of recordings this module depends on.
 */
static const char *const s_all_fragments[] = {
    "It is", "oh", "o'clock",
    "in the morning.", "in the afternoon.", "in the evening.",
    "Today is", "the",
    "minus", "degrees Fahrenheit", "degrees Celsius", "in here.",
    "Humidity is", "percent.",
    "Volume up to", "Volume down to",
    "About", "minute left.", "minutes left.", "second left.", "seconds left.",
    "minutes and",
    "That's", "hundred", "thousand", "point", "The answer is",
    "I have been awake for", "minute.", "minutes.", "hour.", "hours.", "and",
    "I have", "kilobytes of fast memory free.",
    "I am online. My address ends in", "I am not on Wi-Fi at the moment.",
    "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday",
    "January", "February", "March", "April", "May", "June",
    "July", "August", "September", "October", "November", "December",
};

/* ------------------------------------------------------------------------ */
/* Building a plan                                                           */
/* ------------------------------------------------------------------------ */

void voice_speech_plan_reset(voice_speech_plan_t *plan)
{
    if (plan == NULL) {
        return;
    }
    plan->count = 0;
    for (size_t i = 0; i < VOICE_SPEECH_MAX_PARTS; i++) {
        plan->parts[i] = NULL;
    }
}

bool voice_speech_plan_add(voice_speech_plan_t *plan, const char *fragment)
{
    if (plan == NULL) {
        return false;
    }

    /*
     * Once a plan has gone wrong it stays wrong.
     *
     * count is set to the sentinel below rather than merely refusing the
     * addition, because the caller builds a plan as a run of calls and only
     * checks at the end. Silently dropping one fragment would leave a sentence
     * that is grammatical, sayable, and missing its number — "it is three in
     * the afternoon" at a quarter to four. Better mute than wrong.
     */
    if (fragment == NULL || plan->count >= VOICE_SPEECH_MAX_PARTS) {
        plan->count = VOICE_SPEECH_MAX_PARTS + 1;   /* Poisoned. */
        return false;
    }
    if (plan->count > VOICE_SPEECH_MAX_PARTS) {
        return false;   /* Already poisoned; leave it. */
    }

    plan->parts[plan->count++] = fragment;
    return true;
}

/**
 * @brief Say a number below one thousand, composing it if it is not in the bank.
 *
 * The bank stops at 120, which covers everything the clock and the thermometer
 * need. Beyond that the number is built the way it is read aloud: the hundreds
 * digit, the word "hundred", then "and" and whatever is left — and whatever is
 * left is always under a hundred, which IS in the bank. Two extra recordings
 * buy the whole range.
 */
static bool add_below_thousand(voice_speech_plan_t *plan, int value)
{
    if (value < 0 || value >= 1000) {
        return voice_speech_plan_add(plan, NULL);   /* Poison the plan. */
    }

    const char *direct = voice_number_words(value);
    if (direct != NULL) {
        return voice_speech_plan_add(plan, direct);
    }

    const int hundreds = value / 100;
    const int rest     = value % 100;

    voice_speech_plan_add(plan, voice_number_words(hundreds));
    voice_speech_plan_add(plan, VOICE_FRAGMENT_HUNDRED);

    if (rest != 0) {
        voice_speech_plan_add(plan, VOICE_FRAGMENT_AND);
        voice_speech_plan_add(plan, voice_number_words(rest));
    }

    return voice_speech_plan_is_usable(plan);
}

bool voice_speech_plan_add_number(voice_speech_plan_t *plan, long value)
{
    if (plan == NULL) {
        return false;
    }

    if (value < 0) {
        voice_speech_plan_add(plan, VOICE_FRAGMENT_MINUS);
        value = -value;
    }

    /*
     * A million is where this stops, and stopping is the right answer.
     * "Nine hundred and ninety nine thousand..." is already at the edge of
     * what anyone follows when it is spoken rather than shown, and the display
     * has the exact figure either way.
     */
    if (value >= 1000000L) {
        return voice_speech_plan_add(plan, NULL);   /* Poison the plan. */
    }

    const int thousands = (int)(value / 1000L);
    const int rest      = (int)(value % 1000L);

    if (thousands > 0) {
        add_below_thousand(plan, thousands);
        voice_speech_plan_add(plan, VOICE_FRAGMENT_THOUSAND);

        if (rest == 0) {
            return voice_speech_plan_is_usable(plan);
        }
        /*
         * "and" before a remainder under a hundred, the way it is said:
         * "two thousand and five", but "two thousand one hundred and five".
         */
        if (rest < 100) {
            voice_speech_plan_add(plan, VOICE_FRAGMENT_AND);
        }
    }

    add_below_thousand(plan, rest);
    return voice_speech_plan_is_usable(plan);
}

bool voice_speech_plan_add_decimal(voice_speech_plan_t *plan, double value)
{
    if (plan == NULL) {
        return false;
    }

    if (value < 0.0) {
        voice_speech_plan_add(plan, VOICE_FRAGMENT_MINUS);
        value = -value;
    }

    /*
     * Round to the places the display shows, so the spoken answer and the
     * written one cannot disagree. Without this, 0.1 + 0.2 would show 0.3 and
     * say "zero point two nine nine nine..." — which is arithmetically honest
     * and completely useless.
     */
    const long whole = (long)value;
    double fraction = value - (double)whole;

    voice_speech_plan_add_number(plan, whole);

    /* Below a thousandth of a unit, nobody wants to hear the rest. */
    if (fraction < 0.0005) {
        return voice_speech_plan_is_usable(plan);
    }

    voice_speech_plan_add(plan, VOICE_FRAGMENT_POINT);

    /*
     * Digit by digit, which is how a decimal is read aloud: "three point one
     * four", never "three and fourteen hundredths". Three places is as far as
     * anyone listens.
     */
    for (int place = 0; place < 3; place++) {
        fraction *= 10.0;
        const int digit = (int)(fraction + 1e-9);

        voice_speech_plan_add(plan, voice_number_words(digit % 10));
        fraction -= (double)digit;

        if (fraction < 0.0005) {
            break;
        }
    }

    return voice_speech_plan_is_usable(plan);
}

bool voice_speech_plan_is_usable(const voice_speech_plan_t *plan)
{
    return (plan != NULL) && (plan->count > 0) && (plan->count <= VOICE_SPEECH_MAX_PARTS);
}

/* ------------------------------------------------------------------------ */
/* Lookups                                                                   */
/* ------------------------------------------------------------------------ */

const char *voice_speech_weekday(int index)
{
    if (index < 0 || index >= (int)(sizeof(s_weekdays) / sizeof(s_weekdays[0]))) {
        return NULL;
    }
    return s_weekdays[index];
}

const char *voice_speech_month(int index)
{
    if (index < 0 || index >= (int)(sizeof(s_months) / sizeof(s_months[0]))) {
        return NULL;
    }
    return s_months[index];
}

const char *const *voice_speech_all_fragments(size_t *count)
{
    if (count != NULL) {
        *count = sizeof(s_all_fragments) / sizeof(s_all_fragments[0]);
    }
    return s_all_fragments;
}
