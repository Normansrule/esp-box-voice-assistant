/*
 * buddy_api_key.h — Reading and sanity-checking an OpenAI API key.
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 *
 * WHY THIS IS ITS OWN FILE
 * ------------------------
 * A wrong API key and a *truncated* API key fail in exactly the same way: the
 * service answers 401 and the box says "my online brain refused the request".
 * That is a miserable thing to debug, because the one piece of information that
 * would settle it — how many characters actually got stored — is invisible from
 * the outside.
 *
 * So the handling of the key is pulled out into a module with no hardware and
 * no ESP-IDF dependencies at all. It can be compiled and tested on an ordinary
 * computer, and the host tests do exactly that. Nothing in here talks to flash,
 * the network, or the screen.
 *
 * WHAT A KEY LOOKS LIKE
 * ---------------------
 * OpenAI has issued three shapes over time, and the box has to accept all of
 * them:
 *
 *   sk-<48 chars>              the original, 51 characters in total
 *   sk-proj-<...>              project keys, around 165 characters
 *   sk-svcacct-<...>           service-account keys, longer still
 *
 * The middle one is the one that matters: it is what the console hands out
 * today, and it is more than three times the length of the format most code was
 * written against.
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * The shortest thing that could possibly be a key.
 *
 * Set from the original "sk-" + 48 format. Anything shorter than this is a
 * typo, a placeholder, or an empty line — not a key.
 */
#define BUDDY_API_KEY_MIN_PLAUSIBLE_LEN     40

/**
 * @brief Tidy a raw API key into something that can go in a header.
 *
 * A key pasted into a text editor arrives with a trailing newline, usually with
 * a carriage return in front of it on Windows, and sometimes with a stray space
 * at one end from the copy. Any one of those makes the Authorization header
 * invalid, and the resulting 401 is indistinguishable from a wrong key.
 *
 * Only the first line is considered, so a file may carry a comment underneath
 * the key. Leading and trailing whitespace is removed. Whatever survives is
 * copied out verbatim — no case changes, no character filtering, because the
 * key is opaque and guessing at its alphabet is how you break the next format.
 *
 * @param raw       Text as read from the file. NULL is treated as empty.
 * @param out       Destination. Always NUL-terminated, including on failure.
 * @param out_size  Size of @p out in bytes. Must be at least 1.
 *
 * @return Characters written, not counting the terminator. Zero means there was
 *         nothing usable, or the key did not fit — see the note below.
 *
 * @note A key too long for @p out is rejected outright rather than truncated.
 *       Truncation is the bug this whole module exists to prevent: a short key
 *       that is obviously absent beats a long one that is quietly wrong.
 */
size_t buddy_api_key_sanitise(const char *raw, char *out, size_t out_size);

/**
 * @brief A cheap sanity check on a key's shape.
 *
 * Deliberately loose. It answers "could this plausibly be a key?" so the boot
 * log and the self test can say something useful, and it must never reject a
 * real key it has not seen the format of before. Only the "sk-" prefix and a
 * minimum length are required.
 *
 * @param key  NUL-terminated key, may be NULL.
 */
bool buddy_api_key_looks_plausible(const char *key);

/**
 * @brief Describe a key for a log line or the self test, without revealing it.
 *
 * Produces something like "sk-proj-... 164 chars" — enough to tell a full key
 * from a truncated one at a glance, while never putting the secret itself into
 * a log that might get pasted into a forum post.
 *
 * @param key       NUL-terminated key, may be NULL or empty.
 * @param out       Destination for the description.
 * @param out_size  Size of @p out in bytes.
 */
void buddy_api_key_describe(const char *key, char *out, size_t out_size);

#ifdef __cplusplus
}
#endif
