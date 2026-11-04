/*
 * face_theme.c — The active colour theme.
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 *
 * Both palettes are const, so they live in flash. The only RAM this module
 * uses is the one-byte theme selector.
 */

/* CONFIG_* values arrive through lvgl.h, which includes sdkconfig.h on ESP-IDF. */
#include "face_theme.h"

static const uint32_t s_palettes[FACE_THEME_COUNT][FACE_TOKEN_COUNT] = {
    [FACE_THEME_PAPER] = {
        [FACE_TOKEN_FACE]           = FACE_HEX_ESPRESSIF_RED,
        [FACE_TOKEN_ACCENT]         = FACE_HEX_ACCENT,
        [FACE_TOKEN_ACCENT_PRESSED] = FACE_HEX_ESPRESSIF_RED_DARK,
        [FACE_TOKEN_BLUSH]          = FACE_HEX_BLUSH,
        [FACE_TOKEN_BACKGROUND]     = FACE_HEX_BACKGROUND,
        [FACE_TOKEN_TEXT]           = FACE_HEX_TEXT,
        [FACE_TOKEN_TEXT_MUTED]     = FACE_HEX_TEXT_MUTED,
        [FACE_TOKEN_SURFACE]        = FACE_HEX_SURFACE,
        [FACE_TOKEN_SURFACE_BORDER] = FACE_HEX_SURFACE_BORDER,
        [FACE_TOKEN_ON_ACCENT]      = FACE_HEX_ON_ACCENT,
    },
    [FACE_THEME_EMBER] = {
        [FACE_TOKEN_FACE]           = FACE_NIGHT_HEX_ESPRESSIF_RED,
        [FACE_TOKEN_ACCENT]         = FACE_NIGHT_HEX_ACCENT,
        [FACE_TOKEN_ACCENT_PRESSED] = FACE_NIGHT_HEX_ESPRESSIF_RED_DARK,
        [FACE_TOKEN_BLUSH]          = FACE_NIGHT_HEX_BLUSH,
        [FACE_TOKEN_BACKGROUND]     = FACE_NIGHT_HEX_BACKGROUND,
        [FACE_TOKEN_TEXT]           = FACE_NIGHT_HEX_TEXT,
        [FACE_TOKEN_TEXT_MUTED]     = FACE_NIGHT_HEX_TEXT_MUTED,
        [FACE_TOKEN_SURFACE]        = FACE_NIGHT_HEX_SURFACE,
        [FACE_TOKEN_SURFACE_BORDER] = FACE_NIGHT_HEX_SURFACE_BORDER,
        [FACE_TOKEN_ON_ACCENT]      = FACE_NIGHT_HEX_ON_ACCENT,
    },
};

#if defined(CONFIG_BUDDY_THEME_EMBER) && CONFIG_BUDDY_THEME_EMBER
static uint8_t s_theme = FACE_THEME_EMBER;
#else
static uint8_t s_theme = FACE_THEME_PAPER;
#endif

void face_theme_set(face_theme_id_t theme)
{
    if ((unsigned)theme < FACE_THEME_COUNT) {
        s_theme = (uint8_t)theme;
    }
}

face_theme_id_t face_theme_get(void)
{
    return (face_theme_id_t)s_theme;
}

uint32_t face_theme_hex(face_token_t token)
{
    if ((unsigned)token >= FACE_TOKEN_COUNT) {
        return s_palettes[s_theme][FACE_TOKEN_TEXT];
    }
    return s_palettes[s_theme][token];
}

void face_theme_apply_lvgl(void)
{
#if LV_USE_THEME_DEFAULT
    lv_disp_t *disp = lv_disp_get_default();
    if (disp == NULL) {
        return;
    }
    lv_theme_t *theme = lv_theme_default_init(disp,
                                              FACE_COLOR_ACCENT,
                                              FACE_COLOR_TEXT_MUTED,
                                              s_theme == FACE_THEME_EMBER,
                                              LV_FONT_DEFAULT);
    lv_disp_set_theme(disp, theme);
#endif
}
