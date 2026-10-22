/*
 * ui_screen_face.c — Assembling the home screen.
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 *
 * This module is the boundary between "code that runs on the LVGL task" and
 * "code that runs anywhere else". Every function here takes the display lock,
 * so the voice pipeline and the network callbacks can change the face without
 * ever thinking about it.
 */

#include "esp_log.h"

#include "buddy_config.h"
#include "buddy_settings.h"
#include "face_animator.h"
#include "face_theme.h"
#include "face_view.h"
#include "hw_display.h"
#include "ui_screen_face.h"
#include "ui_sensor_readout.h"
#include "ui_settings.h"
#include "ui_speech_bubble.h"
#include "ui_timer_badge.h"

static const char *TAG = "ui_face";

/**
 * @brief The screen itself was pressed.
 *
 * Only fires when nothing on top claimed the touch, so the gear button and the
 * settings pages are automatically excluded.
 */
static void screen_pressed_cb(lv_event_t *event)
{
    (void)event;

    if (!buddy_settings_get()->touch_antics_enabled) {
        return;
    }
    if (ui_settings_is_open()) {
        return;
    }

    /* Somebody is here, so shake off any sleepiness before reacting. */
    face_animator_notice_activity();
    face_animator_play_random_antic();

    ESP_LOGD(TAG, "Poked");
}

esp_err_t ui_screen_face_create(void)
{
    if (!hw_display_lock(0)) {
        return ESP_ERR_TIMEOUT;
    }

    lv_obj_t *screen = lv_scr_act();

    /* A clean white field. The face is the only thing with any colour on it. */
    lv_obj_set_style_bg_color(screen, FACE_COLOR_BACKGROUND, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_pad_all(screen, 0, LV_PART_MAIN);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

    /* Build from the back forwards: face, then the corner widgets on top. */
    esp_err_t err = face_view_create(screen);
    if (err != ESP_OK) {
        hw_display_unlock();
        return err;
    }

    ui_sensor_readout_create(screen);
    ui_speech_bubble_create(screen);
    ui_timer_badge_create(screen);
    ui_settings_create_button(screen);

    /* Taps anywhere unclaimed reach the screen and make the buddy react. */
    lv_obj_add_flag(screen, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(screen, screen_pressed_cb, LV_EVENT_PRESSED, NULL);

    /*
     * Apply the saved preferences INSIDE the lock, before releasing it.
     *
     * All three of these touch LVGL — showing or hiding the readout, and
     * creating and re-arming the animator's timers — and this runs on the
     * app_main task while the LVGL port task is already spinning on another
     * core. Doing them after the unlock meant two tasks editing LVGL's timer
     * list at once, which is the kind of race that shows up as a boot that
     * hangs once a fortnight and never reproduces on demand.
     */
    const buddy_settings_t *settings = buddy_settings_get();
    ui_sensor_readout_set_visible(settings->show_sensor_readout);

    err = face_animator_start();
    face_animator_set_idle_antics_enabled(settings->idle_antics_enabled);

    hw_display_unlock();

    ESP_LOGI(TAG, "Home screen ready");
    return err;
}

/* ------------------------------------------------------------------------ */
/* Thread-safe wrappers                                                      */
/* ------------------------------------------------------------------------ */

void ui_screen_face_set_emotion(face_emotion_t emotion)
{
    if (hw_display_lock(500)) {
        face_animator_notice_activity();
        face_animator_set_emotion(emotion, FACE_EXPRESSION_TRANSITION_MS);
        hw_display_unlock();
    }
}

void ui_screen_face_show_caption(ui_speech_source_t source, const char *text,
                                 uint32_t auto_hide_ms)
{
    if (text == NULL) {
        return;
    }
    if (hw_display_lock(500)) {
        ui_speech_bubble_show(source, text, auto_hide_ms);
        hw_display_unlock();
    }
}

void ui_screen_face_hide_caption(void)
{
    if (hw_display_lock(500)) {
        ui_speech_bubble_hide();
        hw_display_unlock();
    }
}

void ui_screen_face_set_speaking(bool speaking)
{
    if (hw_display_lock(500)) {
        face_animator_set_speaking(speaking);
        hw_display_unlock();
    }
}

void ui_screen_face_play_antic(void)
{
    if (hw_display_lock(500)) {
        face_animator_notice_activity();
        face_animator_play_random_antic();
        hw_display_unlock();
    }
}

void ui_screen_face_open_settings(void)
{
    if (hw_display_lock(500)) {
        ui_settings_open();
        hw_display_unlock();
    }
}

void ui_screen_face_open_examples(void)
{
    if (hw_display_lock(500)) {
        ui_settings_open_examples();
        hw_display_unlock();
    }
}
