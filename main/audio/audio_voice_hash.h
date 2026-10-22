/*
 * audio_voice_hash.h — Naming a spoken clip after the words it says.
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 *
 * The buddy's offline voice is a folder of small MP3s, each named after a hash
 * of the exact reply it speaks:
 *
 *     "Oh hey, you are back."  ->  /spiffs/voice/5b489fae.mp3
 *
 * The names are computed twice: once on a laptop by
 * scripts/make_voice_clips.py when the clips are rendered, and once on the chip
 * by audio_voice.c when a reply is about to be shown. If the two ever disagreed
 * the buddy would go quiet, so the function lives in this file — free of any
 * ESP-IDF dependency — purely so the host tests can check it against the values
 * Python produces.
 *
 * FNV-1a was picked for exactly one reason: it is four lines long in both
 * languages, so there is nowhere for an incompatibility to hide.
 */

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 32-bit FNV-1a over the bytes of @p text.
 *
 * @param text  Any string. NULL is treated as empty, and returns the basis.
 */
uint32_t audio_voice_hash(const char *text);

#ifdef __cplusplus
}
#endif
