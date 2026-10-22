/*
 * ui_timer_badge.c — The countdown pill in the top-left corner.
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */

#include <stdio.h>

#include "esp_log.h"

#include "buddy_config.h"
#include "buddy_timer.h"
#include "face_theme.h"
#include "hw_display.h"
#include "ui_timer_badge.h"

static const char *TAG = "ui_timer";

/** Below this many seconds the badge turns red, as a last-stretch warning. */
#define URGENT_BELOW_SECONDS        30

static lv_obj_t *s_badge;
static lv_obj_t *s_label;

esp_err_t ui_timer_badge_create(lv_obj_t *parent)
{
    if (parent == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_badge != NULL) {
        return ESP_OK;
    }

    s_badge = lv_obj_create(parent);
    lv_obj_remove_style_all(s_badge);
    lv_obj_clear_flag(s_badge, LV_OBJ_FLAG_SCROLLABLE);
    /* Taps fall through to the face, same as the temperature pill. */
    lv_obj_clear_flag(s_badge, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_set_size(s_badge, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(s_badge, FACE_COLOR_SURFACE, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_badge, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(s_badge, 12, LV_PART_MAIN);
    lv_obj_set_style_pad_hor(s_badge, 9, LV_PART_MAIN);
    lv_obj_set_style_pad_ver(s_badge, 4, LV_PART_MAIN);

    /*
     * Same size and inset as the sensor pill on the other side. Matching them
     * is what makes the two read as a pair of status chips rather than two
     * unrelated things that happen to be near the top.
     */
    lv_obj_align(s_badge, LV_ALIGN_TOP_LEFT,
                 BUDDY_SCREEN_EDGE_MARGIN, BUDDY_SCREEN_EDGE_MARGIN);

    s_label = lv_label_create(s_badge);
    lv_obj_set_style_text_font(s_label, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_label, FACE_COLOR_TEXT_MUTED, LV_PART_MAIN);
    lv_label_set_text(s_label, "0:00");

    lv_obj_add_flag(s_badge, LV_OBJ_FLAG_HIDDEN);

    ESP_LOGI(TAG, "Timer badge created");
    return ESP_OK;
}

void ui_timer_badge_update(uint32_t seconds_left, bool running)
{
    if (s_badge == NULL) {
        return;
    }

    /*
     * Called straight from the timer's one-second tick, which runs on the
     * esp_timer task rather than the LVGL one. Touching widgets from another
     * task without this lock is the classic source of rare, unreproducible
     * display corruption.
     */
    if (!hw_display_lock(100)) {
        return;
    }

    if (!running) {
        lv_obj_add_flag(s_badge, LV_OBJ_FLAG_HIDDEN);
        hw_display_unlock();
        return;
    }

    char clock[16];
    buddy_timer_format_clock(seconds_left, clock, sizeof(clock));
    lv_label_set_text(s_label, clock);

    lv_obj_set_style_text_color(s_label,
                                (seconds_left <= URGENT_BELOW_SECONDS)
                                    ? FACE_COLOR_ESPRESSIF_RED
                                    : FACE_COLOR_TEXT_MUTED,
                                LV_PART_MAIN);

    lv_obj_clear_flag(s_badge, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_badge);

    hw_display_unlock();
}
