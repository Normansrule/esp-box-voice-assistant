/*
 * voice_mood_tag.h — Reading the mood the online brain asked for.
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 *
 * The buddy asks the language model to begin every reply with a mood in square
 * brackets:
 *
 *     [curious] That is a good question, actually.
 *
 * Parsing that back out is what lets a cloud answer arrive wearing the right
 * face. It is a small job, but it runs on text a remote service produced, so it
 * has to cope calmly with the model ignoring the instruction, inventing a mood
 * that does not exist, opening a bracket and never closing it, or returning
 * nothing at all.
 *
 * It lives in its own file precisely so it can be tested directly, without
 * dragging in the network stack. See tests/test_desk_buddy.c.
 */

#pragma once

#include "face_emotions.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Split a mood tag off the front of a reply.
 *
 * @param raw      What the model returned. NULL is tolerated.
 * @param emotion  Receives the mood. Set to a sensible default when there is no
 *                 usable tag, so it is always safe to use.
 *
 * @return A pointer INTO @p raw at the first character of the actual answer.
 *         Never NULL. When there is no recognisable tag the whole string is
 *         returned unchanged — losing the reply would be far worse than showing
 *         a stray bracket.
 */
const char *voice_mood_tag_split(const char *raw, face_emotion_t *emotion);

#ifdef __cplusplus
}
#endif
