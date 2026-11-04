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
#include "esp_timer.h"

#include "buddy_config.h"
#include "buddy_settings.h"
#include "face_animator.h"
#include "face_mood.h"
#include "face_theme.h"
#include "face_view.h"
#include "hw_buttons.h"
#include "hw_display.h"
#include "ui_screen_face.h"
#include "ui_sensor_readout.h"
#include "ui_settings.h"
#include "ui_speech_bubble.h"
#include "ui_timer_badge.h"

static const char *TAG = "ui_face";

/** How often the mute switch position is checked, on the LVGL task. */
#define MUTE_POLL_MS        150
#define MUTE_CAPTION_MS     2600

static face_tickle_t s_tickle;
static bool          s_shown_muted;
static bool          s_mute_known;

/**
 * @brief Mirror the physical mute switch onto the face.
 *
 * Polled on an LVGL timer rather than pushed from the button callback: the
 * button driver runs on another task, and polling here means the face is only
 * ever touched with the LVGL lock already held, by LVGL itself. 150 ms is far
 * quicker than anyone can look up from flipping the switch.
 *
 * The source of truth is hw_buttons_is_muted() - the same state that gates
 * capture in hw_microphone - so the face can only say "muted" when the
 * microphone really is returning silence.
 */
static void mute_poll_cb(lv_timer_t *timer)
{
    (void)timer;

    const bool muted = hw_buttons_is_muted();
    if (s_mute_known && muted == s_shown_muted) {
        return;
    }
    const bool first_look = !s_mute_known;
    s_mute_known = true;
    s_shown_muted = muted;

    face_animator_set_muted(muted);

    /* At boot, only speak up if the switch is already on: "on" is normal. */
    if (first_look && !muted) {
        return;
    }
    ui_speech_bubble_show(UI_SPEECH_STATUS,
                          muted ? "Microphone off. I can't hear you."
                                : "Microphone on.",
                          MUTE_CAPTION_MS);
    if (!muted) {
        /* A small "hello again" on the way back, once the face has settled. */
        face_animator_flash_emotion(FACE_EMOTION_HAPPY, 900);
    }
}

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

    /*
     * One tap gets one random antic, as before. A quick run of taps is a
     * tickle: the giggles, and if it keeps going, a scrunched-up "enough".
     * While muted the face holds still (see face_animator_set_muted), so a
     * tickle gets the same treatment as a poke there - nothing happens.
     */
    const uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000);
    switch (face_tickle_register(&s_tickle, now_ms)) {
    case FACE_TICKLE_GIGGLE: {
        const int giggle = face_antic_find("giggle fit");
        if (giggle >= 0) {
            face_animator_play_antic((uint8_t)giggle);
        }
        ESP_LOGD(TAG, "Tickled");
        break;
    }
    case FACE_TICKLE_ENOUGH:
        if (!face_animator_is_muted()) {
            face_animator_flash_emotion(FACE_EMOTION_SCRUNCH, 1200);
            ui_speech_bubble_show(UI_SPEECH_FROM_BUDDY, "Okay, okay! I give up!", 1800);
        }
        ESP_LOGD(TAG, "Tickled too much");
        break;
    case FACE_TICKLE_POKE:
    default:
        face_animator_play_random_antic();
        ESP_LOGD(TAG, "Poked");
        break;
    }
}

esp_err_t ui_screen_face_create(void)
{
    if (!hw_display_lock(0)) {
        return ESP_ERR_TIMEOUT;
    }

    /*
     * Point LVGL's own widget theme (keyboard, sliders, dropdowns) at the
     * active palette before anything is built, so the settings pages and the
     * home screen agree on one accent and on light or dark.
     */
    face_theme_apply_lvgl();

    lv_obj_t *screen = lv_scr_act();

    /* A plain field in the theme's background. The face carries the colour. */
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

    /* Show the mute switch on the face, starting with where it sits now. */
    if (lv_timer_create(mute_poll_cb, MUTE_POLL_MS, NULL) == NULL) {
        ESP_LOGW(TAG, "No mute indicator timer: the face will not show mute");
    }

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

void ui_screen_face_set_gaze_bias(int16_t look_x, int16_t look_y)
{
    /* Short timeout: a skipped frame of gaze is invisible, a stall is not. */
    if (hw_display_lock(20)) {
        face_view_set_gaze_bias(look_x, look_y);
        hw_display_unlock();
    }
}

void ui_screen_face_flash_emotion(face_emotion_t emotion, uint16_t hold_ms)
{
    if (hw_display_lock(500)) {
        face_animator_notice_activity();
        face_animator_flash_emotion(emotion, hold_ms);
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
