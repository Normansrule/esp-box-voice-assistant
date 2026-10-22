/*
 * face_theme.h — The colour palette the buddy is drawn with.
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 *
 * Everything on screen uses one of these colours, so re-skinning the buddy is a
 * matter of editing this file only.
 */

#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Espressif brand red. This is the colour of the eyes, eyebrows and mouth.
 * If you want to match a particular print or logo exactly, change it here.
 */
#define FACE_HEX_ESPRESSIF_RED      0xE8362D

/** A darker red used for subtle depth (pressed states, shadows). */
#define FACE_HEX_ESPRESSIF_RED_DARK 0xB8241C

/** A pale red used for the blush circles on happy expressions. */
#define FACE_HEX_BLUSH              0xF7ADA8

/** Clean white background, as requested — the face floats on it. */
#define FACE_HEX_BACKGROUND         0xFFFFFF

/** Near-black used for readable body text (sensor readout, settings). */
#define FACE_HEX_TEXT               0x2B2B2B

/** Muted grey for secondary text and hairlines. */
#define FACE_HEX_TEXT_MUTED         0x8A8F98

/** Very light grey used as the fill of chips, cards and the settings button. */
#define FACE_HEX_SURFACE            0xF2F3F5

/** Slightly darker grey for the borders of those surfaces. */
#define FACE_HEX_SURFACE_BORDER     0xE0E2E6

/* Convenience wrappers so call sites read as colours, not as numbers. */
#define FACE_COLOR_ESPRESSIF_RED        lv_color_hex(FACE_HEX_ESPRESSIF_RED)
#define FACE_COLOR_ESPRESSIF_RED_DARK   lv_color_hex(FACE_HEX_ESPRESSIF_RED_DARK)
#define FACE_COLOR_BLUSH                lv_color_hex(FACE_HEX_BLUSH)
#define FACE_COLOR_BACKGROUND           lv_color_hex(FACE_HEX_BACKGROUND)
#define FACE_COLOR_TEXT                 lv_color_hex(FACE_HEX_TEXT)
#define FACE_COLOR_TEXT_MUTED           lv_color_hex(FACE_HEX_TEXT_MUTED)
#define FACE_COLOR_SURFACE              lv_color_hex(FACE_HEX_SURFACE)
#define FACE_COLOR_SURFACE_BORDER       lv_color_hex(FACE_HEX_SURFACE_BORDER)

#ifdef __cplusplus
}
#endif
