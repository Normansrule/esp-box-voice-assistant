/*
 * face_theme.h — The colours the buddy is drawn with, in two themes.
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 *
 * Everything on screen uses one of these colours, so re-skinning the buddy is
 * a matter of editing this file only.
 *
 * Two themes, picked for where the box actually sits:
 *
 *   PAPER  (day)   warm off-white, ink text. Less glare than pure white on a
 *                  backlit panel, and the red face still pops.
 *   EMBER  (night) warm near-black, a brighter vermilion face. For a desk at
 *                  night, where a white screen lights up the whole room.
 *
 * Every text/background pair below is checked against WCAG 2 contrast (4.5:1
 * for text this size); the numbers are in docs/DESIGN.md. The old palette
 * failed two of them: grey text on the chips was 2.9:1 and red text on the
 * caption bubble 3.8:1.
 *
 * The FACE_HEX_* literals are the PAPER palette. tools/extract_firmware_data.py
 * reads them for the web simulator, so keep them as plain hex numbers.
 * Code should use the FACE_COLOR_* macros, which follow the active theme.
 */

#pragma once

#include <stdint.h>
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- PAPER (day) --------------------------------------------------------- */

/** The face itself: Espressif brand red. Eyes, brows and mouth. */
#define FACE_HEX_ESPRESSIF_RED      0xE8362D
/** UI accent: a deeper red that is readable as text (5.1:1 on paper). */
#define FACE_HEX_ACCENT             0xC42A20
/** Darker red for pressed states. */
#define FACE_HEX_ESPRESSIF_RED_DARK 0x9E1F17
/** Blush circles on happy expressions. */
#define FACE_HEX_BLUSH              0xF3B4AB
/** Warm paper the face floats on. */
#define FACE_HEX_BACKGROUND         0xF6F3EE
/** Ink: body text. */
#define FACE_HEX_TEXT               0x1E1B18
/** Secondary text and hairlines (5.6:1 on a chip). */
#define FACE_HEX_TEXT_MUTED         0x5F5952
/** Chips, caption sheet, settings rows. */
#define FACE_HEX_SURFACE            0xEBE6DF
/** Pressed chips and the edges of surfaces. */
#define FACE_HEX_SURFACE_BORDER     0xD9D2C8
/** Text or icons sitting ON the accent colour. */
#define FACE_HEX_ON_ACCENT          0xFFFFFF

/* ---- EMBER (night) ------------------------------------------------------- */

#define FACE_NIGHT_HEX_ESPRESSIF_RED      0xFF5B4A
#define FACE_NIGHT_HEX_ACCENT             0xF25A4C
#define FACE_NIGHT_HEX_ESPRESSIF_RED_DARK 0xB9372B
#define FACE_NIGHT_HEX_BLUSH              0x6A2A24
#define FACE_NIGHT_HEX_BACKGROUND         0x13110F
#define FACE_NIGHT_HEX_TEXT               0xF1ECE5
#define FACE_NIGHT_HEX_TEXT_MUTED         0xA39A90
#define FACE_NIGHT_HEX_SURFACE            0x25211D
#define FACE_NIGHT_HEX_SURFACE_BORDER     0x3A342E
#define FACE_NIGHT_HEX_ON_ACCENT          0x13110F

/* ---- Runtime selection --------------------------------------------------- */

typedef enum {
    FACE_THEME_PAPER = 0,   /*!< Day: warm paper, ink text.          */
    FACE_THEME_EMBER,       /*!< Night: warm black, bright vermilion. */
    FACE_THEME_COUNT,
} face_theme_id_t;

typedef enum {
    FACE_TOKEN_FACE = 0,
    FACE_TOKEN_ACCENT,
    FACE_TOKEN_ACCENT_PRESSED,
    FACE_TOKEN_BLUSH,
    FACE_TOKEN_BACKGROUND,
    FACE_TOKEN_TEXT,
    FACE_TOKEN_TEXT_MUTED,
    FACE_TOKEN_SURFACE,
    FACE_TOKEN_SURFACE_BORDER,
    FACE_TOKEN_ON_ACCENT,
    FACE_TOKEN_COUNT,
} face_token_t;

/**
 * @brief Pick the theme. Only affects widgets created AFTER the call, so call
 *        it before the home screen is built (ui_screen_face_create does).
 */
void face_theme_set(face_theme_id_t theme);

/** @brief The theme in use. Defaults to the one chosen in menuconfig. */
face_theme_id_t face_theme_get(void);

/** @brief One colour of the active theme as 0xRRGGBB. Reads flash only. */
uint32_t face_theme_hex(face_token_t token);

/**
 * @brief Point LVGL's built-in widget theme (keyboard, sliders, dropdowns) at
 *        the active palette: accent as the primary colour, dark mode for EMBER.
 *        Takes no new memory; LVGL re-initialises its existing styles in place.
 *        Call with the display lock held.
 */
void face_theme_apply_lvgl(void);

/*
 * Call sites read as colours, not numbers. These names are kept from the old
 * single-palette file so nothing else had to change:
 *
 *   FACE_COLOR_ESPRESSIF_RED  now means "UI accent" (readable as text). The
 *                             face shapes use FACE_COLOR_FACE instead.
 */
#define FACE_COLOR_FACE                 lv_color_hex(face_theme_hex(FACE_TOKEN_FACE))
#define FACE_COLOR_ACCENT               lv_color_hex(face_theme_hex(FACE_TOKEN_ACCENT))
#define FACE_COLOR_ESPRESSIF_RED        FACE_COLOR_ACCENT
#define FACE_COLOR_ESPRESSIF_RED_DARK   lv_color_hex(face_theme_hex(FACE_TOKEN_ACCENT_PRESSED))
#define FACE_COLOR_BLUSH                lv_color_hex(face_theme_hex(FACE_TOKEN_BLUSH))
#define FACE_COLOR_BACKGROUND           lv_color_hex(face_theme_hex(FACE_TOKEN_BACKGROUND))
#define FACE_COLOR_TEXT                 lv_color_hex(face_theme_hex(FACE_TOKEN_TEXT))
#define FACE_COLOR_TEXT_MUTED           lv_color_hex(face_theme_hex(FACE_TOKEN_TEXT_MUTED))
#define FACE_COLOR_SURFACE              lv_color_hex(face_theme_hex(FACE_TOKEN_SURFACE))
#define FACE_COLOR_SURFACE_BORDER       lv_color_hex(face_theme_hex(FACE_TOKEN_SURFACE_BORDER))
#define FACE_COLOR_ON_ACCENT            lv_color_hex(face_theme_hex(FACE_TOKEN_ON_ACCENT))

#ifdef __cplusplus
}
#endif
