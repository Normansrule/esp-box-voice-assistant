/*
 * voice_mood_tag.c — Parsing "[curious] ..." into a mood and a reply.
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */

#include <string.h>

#include "voice_mood_tag.h"

/** Used whenever the reply carries no usable mood tag. */
#define DEFAULT_EMOTION     FACE_EMOTION_HAPPY

/**
 * @brief True for the whitespace a model might put before its tag.
 */
static bool is_leading_space(char character)
{
    return character == ' ' || character == '\t' ||
           character == '\r' || character == '\n';
}

const char *voice_mood_tag_split(const char *raw, face_emotion_t *emotion)
{
    face_emotion_t parsed_emotion = DEFAULT_EMOTION;
    const char *answer = "";

    if (raw == NULL) {
        goto done;
    }

    answer = raw;

    /* Models sometimes lead with a newline. Step over it before looking. */
    while (is_leading_space(*answer)) {
        answer++;
    }

    /* No opening bracket means no tag. Hand back the reply untouched. */
    if (*answer != '[') {
        answer = raw;
        goto done;
    }

    const char *closing_bracket = strchr(answer, ']');
    if (closing_bracket == NULL) {
        /* An opening bracket with no partner — treat it as ordinary text. */
        answer = raw;
        goto done;
    }

    const size_t tag_length = (size_t)(closing_bracket - answer - 1);

    face_emotion_t candidate;
    if (!face_emotion_from_name(answer + 1, tag_length, &candidate)) {
        /*
         * The tag is not a mood we know. Leaving the bracketed text in place is
         * the lesser evil: the user sees something slightly odd rather than
         * silently losing the first few words of the answer.
         */
        answer = raw;
        goto done;
    }

    parsed_emotion = candidate;

    /* Step past the bracket and any spaces between it and the first word. */
    answer = closing_bracket + 1;
    while (*answer == ' ') {
        answer++;
    }

done:
    if (emotion != NULL) {
        *emotion = parsed_emotion;
    }
    return answer;
}
