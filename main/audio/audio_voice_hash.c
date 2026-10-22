/*
 * audio_voice_hash.c — Naming a spoken clip after the words it says.
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 *
 * See the header for why this is its own file rather than a few lines inside
 * audio_voice.c.
 */

#include <stddef.h>

#include "audio_voice_hash.h"

/*
 * The two magic numbers of FNV-1a, 32-bit flavour. They must match the ones in
 * scripts/make_voice_clips.py exactly; the host tests check a handful of known
 * strings against values that script produced, so a change to either side
 * without the other fails the build rather than quietly silencing the buddy.
 */
#define FNV_OFFSET_BASIS            0x811C9DC5u
#define FNV_PRIME                   0x01000193u

uint32_t audio_voice_hash(const char *text)
{
    uint32_t value = FNV_OFFSET_BASIS;

    if (text == NULL) {
        return value;
    }

    /*
     * Hashed as unsigned bytes, not chars. On a compiler where char is signed —
     * which the Xtensa toolchain is — anything above 127 would sign-extend and
     * hash differently from Python's bytes, and the mismatch would only show up
     * on replies containing an accented character.
     */
    for (const unsigned char *cursor = (const unsigned char *)text; *cursor != '\0'; cursor++) {
        value ^= (uint32_t)(*cursor);
        value *= FNV_PRIME;
    }

    return value;
}
