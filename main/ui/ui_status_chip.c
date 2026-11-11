/*
 * ui_status_chip.c — Drawing the live status chip. See the header.
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */

#include <time.h>

#include "esp_log.h"

#include "buddy_config.h"
#include "face_theme.h"
#include "hw_buttons.h"
#include "net_time.h"
#include "ui_status_chip.h"
#include "ui_status_logic.h"
#include "ui_timer_badge.h"
#include "voice_cloud_brain.h"
#include "voice_recorder.h"
#include "voice_wakeword.h"

static const char *TAG = "ui_status";

#if defined(CONFIG_BUDDY_UI_STATUS_CHIP) && CONFIG_BUDDY_UI_STATUS_CHIP

/*
 * 100 ms is fast enough for the meter to follow syllables and slow enough
 * that the redraw is a handful of 3 px bars ten times a second. When nothing
 * changes, nothing is redrawn: every setter below is skipped on equal values.
 */
#define STATUS_POLL_MS      100

#define BAR_WIDTH           3
#define BAR_GAP             2
#define BAR_MIN_HEIGHT      3
#define BAR_MAX_HEIGHT      14

#if defined(CONFIG_BUDDY_UI_STATUS_CLOCK) && CONFIG_BUDDY_UI_STATUS_CLOCK
#define CLOCK_ENABLED       true
#else
#define CLOCK_ENABLED       false
#endif

static lv_obj_t   *s_chip;
static lv_obj_t   *s_label;
static lv_obj_t   *s_meter;
static lv_obj_t   *s_bars[UI_STATUS_LEVEL_BARS];

static ui_status_t s_shown = UI_STATUS_HIDDEN;
static uint8_t     s_lit = 0xFF;
static int         s_clock_minute = -1;
static bool        s_beside_timer;
static bool        s_placed;

/** Colour the chip for a state: accent fill means "the microphone is live". */
static void paint(ui_status_t status)
{
    const bool live = (status == UI_STATUS_LISTENING);
    lv_obj_set_style_bg_color(s_chip, live ? FACE_COLOR_ACCENT : FACE_COLOR_SURFACE, LV_PART_MAIN);

    lv_color_t text = FACE_COLOR_TEXT_MUTED;
    if (status == UI_STATUS_ONLINE) {
        text = FACE_COLOR_ACCENT;     /* Accent on surface: 4.6:1 Paper, 4.8:1 Ember. */
    } else if (status == UI_STATUS_CLOCK) {
        text = FACE_COLOR_TEXT;
    }
    lv_obj_set_style_text_color(s_label, text, LV_PART_MAIN);

    for (int i = 0; i < UI_STATUS_LEVEL_BARS; i++) {
        lv_obj_set_style_bg_color(s_bars[i], FACE_COLOR_ON_ACCENT, LV_PART_MAIN);
    }

    if (live) {
        lv_obj_add_flag(s_label, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_meter, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_clear_flag(s_label, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_meter, LV_OBJ_FLAG_HIDDEN);
    }
}

/** Light the first @p lit bars; unlit ones stay as faint stubs. */
static void set_meter(uint8_t lit)
{
    if (lit == s_lit) {
        return;
    }
    s_lit = lit;
    for (int i = 0; i < UI_STATUS_LEVEL_BARS; i++) {
        const bool on = (i < lit);
        const lv_coord_t h = on ? (lv_coord_t)(BAR_MIN_HEIGHT +
                                  ((BAR_MAX_HEIGHT - BAR_MIN_HEIGHT) * (i + 1)) / UI_STATUS_LEVEL_BARS)
                                : BAR_MIN_HEIGHT;
        lv_obj_set_height(s_bars[i], h);
        lv_obj_set_style_bg_opa(s_bars[i], on ? LV_OPA_COVER : LV_OPA_40, LV_PART_MAIN);
    }
}

/** Sit beside the timer pill when it shows, otherwise take the corner. */
static void place(void)
{
    lv_obj_t *timer = ui_timer_badge_get_obj();
    const bool beside = (timer != NULL) && !lv_obj_has_flag(timer, LV_OBJ_FLAG_HIDDEN);
    if (s_placed && beside == s_beside_timer) {
        return;
    }
    s_placed = true;
    s_beside_timer = beside;
    if (beside) {
        lv_obj_update_layout(timer);   /* It may have only just been un-hidden. */
        lv_obj_align_to(s_chip, timer, LV_ALIGN_OUT_RIGHT_MID, BUDDY_SCREEN_EDGE_MARGIN, 0);
    } else {
        lv_obj_align(s_chip, LV_ALIGN_TOP_LEFT, BUDDY_SCREEN_EDGE_MARGIN, BUDDY_SCREEN_EDGE_MARGIN);
    }
}

static void poll_cb(lv_timer_t *timer)
{
    (void)timer;

    const ui_status_inputs_t inputs = {
        .muted         = hw_buttons_is_muted(),
        .listening     = voice_wakeword_is_listening(),
        .online_busy   = voice_cloud_brain_is_busy(),
        .clock_valid   = net_time_is_synchronised(),
        .clock_enabled = CLOCK_ENABLED,
    };
    const ui_status_t status = ui_status_pick(&inputs);

    if (status != s_shown) {
        ESP_LOGI(TAG, "Status chip: %s",
                 (status == UI_STATUS_MIC_OFF)   ? "mic off" :
                 (status == UI_STATUS_LISTENING) ? "listening" :
                 (status == UI_STATUS_ONLINE)    ? "online" :
                 (status == UI_STATUS_CLOCK)     ? "clock" : "hidden");
        s_shown = status;
        s_clock_minute = -1;
        s_lit = 0xFF;

        if (status == UI_STATUS_HIDDEN) {
            lv_obj_add_flag(s_chip, LV_OBJ_FLAG_HIDDEN);
            return;
        }
        paint(status);
        if (status == UI_STATUS_MIC_OFF) {
            lv_label_set_text(s_label, LV_SYMBOL_MUTE " Mic off");
        } else if (status == UI_STATUS_ONLINE) {
            lv_label_set_text(s_label, LV_SYMBOL_UPLOAD " Online");
        }
        lv_obj_clear_flag(s_chip, LV_OBJ_FLAG_HIDDEN);
        s_placed = false;   /* Width changed: re-anchor. */
    }

    if (status == UI_STATUS_HIDDEN) {
        return;
    }

    if (status == UI_STATUS_LISTENING) {
        set_meter(ui_status_level_bars(voice_recorder_get_live_peak()));
    } else if (status == UI_STATUS_CLOCK) {
        const time_t now = time(NULL);
        struct tm local;
        localtime_r(&now, &local);
        if (local.tm_min != s_clock_minute) {
            s_clock_minute = local.tm_min;
            char text[8];
            ui_status_format_clock(local.tm_hour, local.tm_min, text, sizeof(text));
            lv_label_set_text(s_label, text);
            s_placed = false;
        }
    }

    place();
    lv_obj_move_foreground(s_chip);
}

esp_err_t ui_status_chip_create(lv_obj_t *parent)
{
    if (parent == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_chip != NULL) {
        return ESP_OK;
    }

    /* Same shape, padding and font as the timer and sensor chips: one family. */
    s_chip = lv_obj_create(parent);
    lv_obj_remove_style_all(s_chip);
    lv_obj_clear_flag(s_chip, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(s_chip, LV_OBJ_FLAG_CLICKABLE);   /* Taps reach the face. */
    lv_obj_set_size(s_chip, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(s_chip, FACE_COLOR_SURFACE, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_chip, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(s_chip, 12, LV_PART_MAIN);
    lv_obj_set_style_pad_hor(s_chip, 9, LV_PART_MAIN);
    lv_obj_set_style_pad_ver(s_chip, 4, LV_PART_MAIN);
    lv_obj_set_flex_flow(s_chip, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(s_chip, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    s_label = lv_label_create(s_chip);
    lv_obj_set_style_text_font(s_label, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_label_set_text(s_label, "");

    /*
     * The meter: five bars bottom-aligned in a box as tall as a line of text,
     * so the chip keeps the same height in every state and nothing jumps.
     */
    s_meter = lv_obj_create(s_chip);
    lv_obj_remove_style_all(s_meter);
    lv_obj_clear_flag(s_meter, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(s_meter, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(s_meter,
                    UI_STATUS_LEVEL_BARS * BAR_WIDTH + (UI_STATUS_LEVEL_BARS - 1) * BAR_GAP,
                    lv_font_get_line_height(&lv_font_montserrat_14));
    lv_obj_set_flex_flow(s_meter, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(s_meter, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    lv_obj_set_style_pad_column(s_meter, BAR_GAP, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(s_meter, 2, LV_PART_MAIN);

    for (int i = 0; i < UI_STATUS_LEVEL_BARS; i++) {
        s_bars[i] = lv_obj_create(s_meter);
        lv_obj_remove_style_all(s_bars[i]);
        lv_obj_clear_flag(s_bars[i], LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_size(s_bars[i], BAR_WIDTH, BAR_MIN_HEIGHT);
        lv_obj_set_style_radius(s_bars[i], 1, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(s_bars[i], LV_OPA_40, LV_PART_MAIN);
    }
    lv_obj_add_flag(s_meter, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_chip, LV_OBJ_FLAG_HIDDEN);

    if (lv_timer_create(poll_cb, STATUS_POLL_MS, NULL) == NULL) {
        ESP_LOGW(TAG, "No status chip timer: the chip will stay hidden");
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "Status chip created");
    return ESP_OK;
}

#else  /* Status chip switched off in menuconfig. */

esp_err_t ui_status_chip_create(lv_obj_t *parent)
{
    (void)parent;
    ESP_LOGI(TAG, "Status chip off (menuconfig)");
    return ESP_OK;
}

#endif
