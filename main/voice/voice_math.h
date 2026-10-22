/*
 * voice_math.h — Answering simple sums without asking the internet.
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 *
 * WHY THIS EXISTS
 * ---------------
 * "What is two plus two" is exactly the kind of question that should not need a
 * round trip to a language model. It costs money, it takes two seconds, and it
 * stops working the moment the Wi-Fi drops. This module answers it on the chip,
 * instantly, for free.
 *
 * WHAT IT UNDERSTANDS
 * -------------------
 * One sum, written either in digits or in words:
 *
 *     "what is 2 + 2"                       ->  4
 *     "what's seven times six"              ->  42
 *     "ten minus four"                      ->  6
 *     "twenty divided by eight"             ->  2.5
 *     "what is one hundred and five plus 5" ->  110
 *     "what is 20 percent of 50"            ->  10
 *
 * Anything more complicated than <number> <operator> <number> is deliberately
 * not handled — it returns false and the caller falls through to the cloud
 * brain, which is far better at open-ended questions than any parser this size
 * could be.
 *
 * WHERE IT IS USED
 * ----------------
 *  - On phrases matched offline by MultiNet (VOICE_ACTION_DO_MATH).
 *  - On the transcript of a cloud question, BEFORE the chat request is sent —
 *    so even online, simple sums are answered locally and cost nothing.
 *
 * There is no hardware or ESP-IDF dependency anywhere in here, which is what
 * lets the host tests in tests/ exercise it directly with an ordinary compiler.
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Longest answer this module will ever produce, including the terminator. */
#define VOICE_MATH_ANSWER_MAX_LEN   32

/**
 * @brief Try to answer a simple arithmetic question.
 *
 * @param question     What the user said. Case and punctuation do not matter.
 * @param answer       Filled in with the bare result, e.g. "4" or "2.5".
 *                     Untouched when the function returns false.
 * @param answer_size  Size of @p answer in bytes. Should be at least
 *                     VOICE_MATH_ANSWER_MAX_LEN.
 *
 * @return true when the question was understood AND has a sensible answer.
 *         false for anything else — including division by zero, numbers too
 *         large to be interesting, and questions that are not sums at all.
 */
bool voice_math_try_answer(const char *question, char *answer, size_t answer_size);

#ifdef __cplusplus
}
#endif
