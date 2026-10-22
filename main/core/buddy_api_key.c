/*
 * buddy_api_key.c — Reading and sanity-checking an OpenAI API key.
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 *
 * Pure text handling, no dependencies. See buddy_api_key.h for why this is a
 * module of its own.
 */

#include <stdio.h>
#include <string.h>

#include "buddy_api_key.h"

/**
 * @brief True for the characters that may surround a key in a text file.
 *
 * Written out rather than using isspace() so the behaviour does not depend on
 * the C library's locale, which on some hosts treats other bytes as space.
 */
static bool is_trimmable(char c)
{
    return (c == ' ') || (c == '\t') || (c == '\r') || (c == '\n') ||
           (c == '\v') || (c == '\f');
}

size_t buddy_api_key_sanitise(const char *raw, char *out, size_t out_size)
{
    if (out == NULL || out_size == 0) {
        return 0;
    }
    out[0] = '\0';

    if (raw == NULL) {
        return 0;
    }

    /* Skip anything blank at the front. */
    const char *start = raw;
    while (*start != '\0' && is_trimmable(*start)) {
        start++;
    }

    /*
     * Stop at the first line break. Everything after it belongs to a comment,
     * a second key, or the editor's trailing newline — never to this key.
     */
    const char *end = start;
    while (*end != '\0' && *end != '\n' && *end != '\r') {
        end++;
    }

    /* Then walk back over any trailing blanks on that line. */
    while (end > start && is_trimmable(*(end - 1))) {
        end--;
    }

    const size_t length = (size_t)(end - start);
    if (length == 0) {
        return 0;
    }

    /*
     * Refuse rather than truncate.
     *
     * This is the single most important line in the file. A key cut short is
     * accepted by every check the box makes, stored happily, shown as "Saved",
     * and then rejected by the service on every request for the rest of time.
     * Returning nothing produces "no API key", which is a problem somebody can
     * actually see and fix.
     */
    if (length >= out_size) {
        return 0;
    }

    memcpy(out, start, length);
    out[length] = '\0';
    return length;
}

bool buddy_api_key_looks_plausible(const char *key)
{
    if (key == NULL) {
        return false;
    }

    const size_t length = strlen(key);
    if (length < BUDDY_API_KEY_MIN_PLAUSIBLE_LEN) {
        return false;
    }

    /*
     * Prefix only. Not "sk-proj-", and not a length range: OpenAI has changed
     * both of those before and will again, and a box that rejects next year's
     * key because it was written this year is worse than one that tries and
     * reports the service's own error message.
     */
    return strncmp(key, "sk-", 3) == 0;
}

void buddy_api_key_describe(const char *key, char *out, size_t out_size)
{
    if (out == NULL || out_size == 0) {
        return;
    }

    if (key == NULL || key[0] == '\0') {
        snprintf(out, out_size, "not set");
        return;
    }

    const size_t length = strlen(key);

    /*
     * At most the first eight characters, which covers "sk-proj-" and reveals
     * nothing: the prefix is a format marker, not part of the secret. Never
     * print more, and never print the tail.
     */
    char prefix[9] = { 0 };
    const size_t prefix_length = (length < 8) ? length : 8;
    memcpy(prefix, key, prefix_length);

    if (!buddy_api_key_looks_plausible(key)) {
        snprintf(out, out_size, "%s... %u chars, looks wrong",
                 prefix, (unsigned)length);
        return;
    }

    snprintf(out, out_size, "%s... %u chars", prefix, (unsigned)length);
}
