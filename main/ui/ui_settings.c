/*
 * ui_settings.c — Building the configuration screens.
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 *
 * The layout is deliberately plain: a header with a back arrow, then a vertical
 * scrolling list of rows. Each row is a label on the left and one control on
 * the right. Rows that need typing open a shared full-screen keyboard page
 * rather than trying to squeeze a text box into a 320-pixel-wide row.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "esp_system.h"

#include "audio_music.h"
#include "buddy_api_key.h"
#include "buddy_config.h"
#include "buddy_selftest.h"
#include "buddy_settings.h"
#include "face_animator.h"
#include "face_theme.h"
#include "hw_display.h"
#include "hw_sensors.h"
#include "hw_speaker.h"
#include "net_wifi.h"
#include "ui_calculator.h"
#include "ui_sensor_readout.h"
#include "ui_settings.h"
#include "voice_wakeword.h"
#include "voice_assistant.h"
#include "voice_cloud_brain.h"
#include "voice_commands.h"
#include "voice_local_brain.h"
#include "voice_recorder.h"
#include "voice_speech_gate.h"

static const char *TAG = "ui_settings";

/** Height of the header strip at the top of every settings page. */
#define HEADER_HEIGHT           40

/** Height of one row in the settings list. */
#define ROW_HEIGHT              46

/* ------------------------------------------------------------------------ */
/* State                                                                     */
/* ------------------------------------------------------------------------ */

static lv_obj_t *s_gear_button;
static lv_obj_t *s_main_page;
static lv_obj_t *s_wifi_page;
static lv_obj_t *s_text_entry_page;
static lv_obj_t *s_examples_page;
static lv_obj_t *s_expressions_page;

/** Live handles for controls whose value other code may need to refresh. */
static lv_obj_t *s_wifi_status_label;
static lv_obj_t *s_wifi_network_list;

/*
 * Values on the MAIN page that can change while it is on screen. Held so a
 * timer can repaint them: the Wi-Fi row in particular is written when the page
 * is built and would otherwise still claim "Not connected" after you joined a
 * network from the sub-page and came back.
 */
static lv_obj_t   *s_wifi_row_value;
static lv_obj_t   *s_api_key_row_value;
static lv_obj_t   *s_home_url_row_value;
static lv_obj_t   *s_requests_row_value;
static lv_obj_t   *s_about_label;
static lv_timer_t *s_main_page_timer;

/** What to do with the text once the user finishes typing. */
typedef void (*text_entry_done_cb_t)(const char *text);
static text_entry_done_cb_t s_text_entry_callback;
static lv_obj_t *s_text_entry_field;

/** Remembers which network was tapped while its password is being typed. */
static char s_pending_wifi_ssid[BUDDY_WIFI_SSID_MAX_LEN];
static lv_timer_t *s_wifi_status_timer;

/* Forward declarations. */
static void build_main_page(void);
static void open_wifi_page(void);
static void wifi_status_timer_cb(lv_timer_t *timer);
static void main_page_refresh_cb(lv_timer_t *timer);
static void open_examples_page(void);
static void open_expressions_page(void);
static void open_text_entry(const char *title, const char *initial_text,
                            bool hide_characters, uint32_t max_length,
                            text_entry_done_cb_t on_done);

/* ------------------------------------------------------------------------ */
/* Shared widget builders                                                    */
/* ------------------------------------------------------------------------ */

/**
 * @brief Create a full-screen white page with a header and a back button.
 *
 * @param title      Text shown in the header.
 * @param on_back    Called when the back arrow is tapped.
 * @param body_out   Receives the scrollable area below the header.
 * @return The page container.
 */
static lv_obj_t *create_page(const char *title, lv_event_cb_t on_back, lv_obj_t **body_out)
{
    lv_obj_t *page = lv_obj_create(lv_scr_act());
    lv_obj_remove_style_all(page);
    lv_obj_set_size(page, BUDDY_SCREEN_WIDTH, BUDDY_SCREEN_HEIGHT);
    lv_obj_set_pos(page, 0, 0);
    lv_obj_clear_flag(page, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(page, LV_OBJ_FLAG_CLICKABLE);   /* Swallow taps meant for the face. */
    lv_obj_set_style_bg_color(page, FACE_COLOR_BACKGROUND, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(page, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_move_foreground(page);

    /* --- Header ------------------------------------------------------- */
    lv_obj_t *header = lv_obj_create(page);
    lv_obj_remove_style_all(header);
    lv_obj_set_size(header, BUDDY_SCREEN_WIDTH, HEADER_HEIGHT);
    lv_obj_set_pos(header, 0, 0);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(header, FACE_COLOR_ESPRESSIF_RED, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(header, LV_OPA_COVER, LV_PART_MAIN);

    lv_obj_t *back = lv_btn_create(header);
    lv_obj_remove_style_all(back);
    lv_obj_set_size(back, 44, HEADER_HEIGHT);
    lv_obj_align(back, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_add_event_cb(back, on_back, LV_EVENT_CLICKED, NULL);

    lv_obj_t *back_icon = lv_label_create(back);
    lv_label_set_text(back_icon, LV_SYMBOL_LEFT);
    lv_obj_set_style_text_color(back_icon, lv_color_white(), LV_PART_MAIN);
    lv_obj_center(back_icon);

    lv_obj_t *title_label = lv_label_create(header);
    lv_label_set_text(title_label, title);
    lv_obj_set_style_text_color(title_label, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_font(title_label, &lv_font_montserrat_16, LV_PART_MAIN);
    lv_obj_align(title_label, LV_ALIGN_CENTER, 0, 0);

    /* --- Scrollable body ---------------------------------------------- */
    lv_obj_t *body = lv_obj_create(page);
    lv_obj_remove_style_all(body);
    lv_obj_set_size(body, BUDDY_SCREEN_WIDTH, BUDDY_SCREEN_HEIGHT - HEADER_HEIGHT);
    lv_obj_set_pos(body, 0, HEADER_HEIGHT);
    lv_obj_set_style_bg_color(body, FACE_COLOR_BACKGROUND, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(body, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_pad_all(body, 6, LV_PART_MAIN);
    lv_obj_set_style_pad_row(body, 4, LV_PART_MAIN);
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(body, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(body, LV_SCROLLBAR_MODE_AUTO);

    if (body_out != NULL) {
        *body_out = body;
    }
    return page;
}

/**
 * @brief Create one row of the settings list: a label plus room for a control.
 *
 * @return The row container; add the control to it and align it right.
 */
static lv_obj_t *create_row(lv_obj_t *body, const char *label_text)
{
    lv_obj_t *row = lv_obj_create(body);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, lv_pct(100), ROW_HEIGHT);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(row, FACE_COLOR_SURFACE, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(row, 10, LV_PART_MAIN);
    lv_obj_set_style_pad_hor(row, 10, LV_PART_MAIN);

    lv_obj_t *label = lv_label_create(row);
    lv_label_set_text(label, label_text);
    lv_obj_set_style_text_color(label, FACE_COLOR_TEXT, LV_PART_MAIN);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_16, LV_PART_MAIN);
    lv_obj_align(label, LV_ALIGN_LEFT_MID, 0, 0);

    return row;
}

/**
 * @brief Create a section heading between groups of rows.
 */
static void create_section_heading(lv_obj_t *body, const char *text)
{
    lv_obj_t *heading = lv_label_create(body);
    lv_label_set_text(heading, text);
    lv_obj_set_style_text_color(heading, FACE_COLOR_TEXT_MUTED, LV_PART_MAIN);
    lv_obj_set_style_pad_top(heading, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_left(heading, 4, LV_PART_MAIN);
}

/* ------------------------------------------------------------------------ */
/* The text entry page                                                       */
/* ------------------------------------------------------------------------ */

static void close_text_entry(void)
{
    if (s_text_entry_page != NULL) {
        lv_obj_del(s_text_entry_page);
        s_text_entry_page = NULL;
        s_text_entry_field = NULL;
    }
}

static void text_entry_back_cb(lv_event_t *event)
{
    (void)event;
    s_text_entry_callback = NULL;
    close_text_entry();
}

/**
 * @brief The keyboard's tick and cross buttons both land here.
 */
static void text_entry_keyboard_cb(lv_event_t *event)
{
    const lv_event_code_t code = lv_event_get_code(event);

    if (code == LV_EVENT_READY) {
        /* Tick pressed: hand the text to whoever asked for it. */
        const char *typed = (s_text_entry_field != NULL)
                            ? lv_textarea_get_text(s_text_entry_field) : "";
        text_entry_done_cb_t callback = s_text_entry_callback;
        s_text_entry_callback = NULL;

        if (callback != NULL) {
            callback(typed);
        }
        close_text_entry();
        return;
    }

    if (code == LV_EVENT_CANCEL) {
        s_text_entry_callback = NULL;
        close_text_entry();
    }
}

static void open_text_entry(const char *title, const char *initial_text,
                            bool hide_characters, uint32_t max_length,
                            text_entry_done_cb_t on_done)
{
    close_text_entry();
    s_text_entry_callback = on_done;

    lv_obj_t *body = NULL;
    s_text_entry_page = create_page(title, text_entry_back_cb, &body);

    /* The box being typed into. */
    s_text_entry_field = lv_textarea_create(body);
    lv_obj_set_size(s_text_entry_field, lv_pct(100), 52);
    lv_textarea_set_one_line(s_text_entry_field, true);
    lv_textarea_set_max_length(s_text_entry_field, max_length);
    lv_textarea_set_password_mode(s_text_entry_field, hide_characters);
    lv_textarea_set_text(s_text_entry_field, (initial_text != NULL) ? initial_text : "");
    lv_obj_set_style_text_font(s_text_entry_field, &lv_font_montserrat_16, LV_PART_MAIN);

    /* The keyboard fills the bottom half of the screen. */
    lv_obj_t *keyboard = lv_keyboard_create(s_text_entry_page);
    lv_obj_set_size(keyboard, BUDDY_SCREEN_WIDTH, 132);
    lv_obj_align(keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_keyboard_set_textarea(keyboard, s_text_entry_field);
    lv_obj_add_event_cb(keyboard, text_entry_keyboard_cb, LV_EVENT_READY, NULL);
    lv_obj_add_event_cb(keyboard, text_entry_keyboard_cb, LV_EVENT_CANCEL, NULL);
}

/* ------------------------------------------------------------------------ */
/* Handlers for the individual settings                                      */
/* ------------------------------------------------------------------------ */

static void volume_changed_cb(lv_event_t *event)
{
    lv_obj_t *slider = lv_event_get_target(event);
    const int value = lv_slider_get_value(slider);

    buddy_settings_get()->speaker_volume = (uint8_t)value;
    hw_speaker_set_volume((uint8_t)value);
    buddy_settings_save();
}

static void brightness_changed_cb(lv_event_t *event)
{
    lv_obj_t *slider = lv_event_get_target(event);
    const int value = lv_slider_get_value(slider);

    buddy_settings_get()->screen_brightness = (uint8_t)value;
    hw_display_set_brightness((uint8_t)value);
    buddy_settings_save();
}

static void fahrenheit_toggled_cb(lv_event_t *event)
{
    lv_obj_t *toggle = lv_event_get_target(event);
    const bool on = lv_obj_has_state(toggle, LV_STATE_CHECKED);

    buddy_settings_get()->temperature_unit = on ? BUDDY_TEMPERATURE_FAHRENHEIT
                                                : BUDDY_TEMPERATURE_CELSIUS;
    buddy_settings_save();
    ui_sensor_readout_refresh();
}

static void show_readout_toggled_cb(lv_event_t *event)
{
    lv_obj_t *toggle = lv_event_get_target(event);
    const bool on = lv_obj_has_state(toggle, LV_STATE_CHECKED);

    buddy_settings_get()->show_sensor_readout = on;
    buddy_settings_save();
    ui_sensor_readout_set_visible(on);
}

static void idle_antics_toggled_cb(lv_event_t *event)
{
    lv_obj_t *toggle = lv_event_get_target(event);
    const bool on = lv_obj_has_state(toggle, LV_STATE_CHECKED);

    buddy_settings_get()->idle_antics_enabled = on;
    buddy_settings_save();
    face_animator_set_idle_antics_enabled(on);
}

static void touch_antics_toggled_cb(lv_event_t *event)
{
    lv_obj_t *toggle = lv_event_get_target(event);
    buddy_settings_get()->touch_antics_enabled = lv_obj_has_state(toggle, LV_STATE_CHECKED);
    buddy_settings_save();
}

static void chimes_toggled_cb(lv_event_t *event)
{
    lv_obj_t *toggle = lv_event_get_target(event);
    buddy_settings_get()->chimes_enabled = lv_obj_has_state(toggle, LV_STATE_CHECKED);
    buddy_settings_save();
}

static void cloud_toggled_cb(lv_event_t *event)
{
    lv_obj_t *toggle = lv_event_get_target(event);
    buddy_settings_get()->cloud_brain_enabled = lv_obj_has_state(toggle, LV_STATE_CHECKED);
    buddy_settings_save();
    voice_cloud_brain_reset_conversation();
}

/**
 * @brief Open the keypad, closing Settings behind it.
 *
 * Settings goes away rather than staying underneath, because the calculator is
 * a place you go to work rather than a sub-page you glance at — and a page
 * left open behind a full-screen one is exactly the bug that made tapping a
 * face preview look like a freeze.
 */
static void calculator_row_cb(lv_event_t *event)
{
    (void)event;
    ui_settings_close();
    ui_calculator_open();
}

static void noise_profile_changed_cb(lv_event_t *event)
{
    lv_obj_t *dropdown = lv_event_get_target(event);
    const uint16_t choice = lv_dropdown_get_selected(dropdown);

    buddy_settings_t *settings = buddy_settings_get();
    settings->noise_profile = (choice < BUDDY_NOISE_PROFILE_COUNT)
                              ? (buddy_noise_profile_t)choice
                              : BUDDY_NOISE_AUTOMATIC;
    buddy_settings_save();

    /* Applied immediately — there is no reason to make anyone reboot for this. */
    voice_wakeword_apply_noise_profile(settings->noise_profile);
}

static void brain_mode_changed_cb(lv_event_t *event)
{
    lv_obj_t *dropdown = lv_event_get_target(event);
    const uint16_t choice = lv_dropdown_get_selected(dropdown);

    buddy_settings_t *settings = buddy_settings_get();
    settings->brain_mode = (choice < BUDDY_BRAIN_MODE_COUNT)
                           ? (buddy_brain_mode_t)choice
                           : BUDDY_BRAIN_SMART;
    buddy_settings_save();

    /* Home AI and OpenAI are different servers; start the next chat afresh. */
    voice_cloud_brain_reset_conversation();

    ESP_LOGI(TAG, "Brain mode is now %d", (int)settings->brain_mode);
}

/**
 * The hourly ceilings offered, and the labels for them.
 *
 * A short list of round numbers rather than a slider: this is a spending limit,
 * and the useful question is "roughly how much", not "exactly how many".
 */
static const uint16_t s_request_limits[] = { 10, 30, 60, 120, 0 };

static void request_limit_changed_cb(lv_event_t *event)
{
    lv_obj_t *dropdown = lv_event_get_target(event);
    const uint16_t choice = lv_dropdown_get_selected(dropdown);

    const size_t count = sizeof(s_request_limits) / sizeof(s_request_limits[0]);
    buddy_settings_get()->requests_per_hour = (choice < count) ? s_request_limits[choice]
                                                               : BUDDY_DEFAULT_REQUESTS_PER_HOUR;
    buddy_settings_save();
}

static void music_source_changed_cb(lv_event_t *event)
{
    lv_obj_t *dropdown = lv_event_get_target(event);
    const uint16_t choice = lv_dropdown_get_selected(dropdown);

    buddy_settings_get()->music_source = (choice == 1) ? BUDDY_MUSIC_SOURCE_STREAM
                                                       : BUDDY_MUSIC_SOURCE_FLASH;
    buddy_settings_save();
}

/**
 * @brief Language changed. Applying it means reloading both speech models,
 *        which cannot be done safely while the recogniser is running, so the
 *        change is saved and a restart is offered instead.
 */
static void language_restart_cb(lv_event_t *event)
{
    lv_obj_t *button_row = lv_event_get_target(event);
    lv_obj_t *dialog = lv_obj_get_parent(button_row);

    const char *pressed = lv_msgbox_get_active_btn_text(dialog);

    if (pressed != NULL && strcmp(pressed, "Restart") == 0) {
        esp_restart();
    }
    lv_msgbox_close(dialog);
}

static void language_changed_cb(lv_event_t *event)
{
    lv_obj_t *dropdown = lv_event_get_target(event);
    const uint16_t choice = lv_dropdown_get_selected(dropdown);

    buddy_settings_get()->language = (choice == 1) ? BUDDY_LANGUAGE_CHINESE
                                                   : BUDDY_LANGUAGE_ENGLISH;
    buddy_settings_save();

    static const char *buttons[] = { "Later", "Restart", "" };

    lv_obj_t *dialog = lv_msgbox_create(NULL, "Restart needed",
                                        "The wake word and command models load at "
                                        "start-up. Restart now to switch language?",
                                        buttons, false);
    lv_obj_center(dialog);
    lv_obj_add_event_cb(lv_msgbox_get_btns(dialog), language_restart_cb,
                        LV_EVENT_VALUE_CHANGED, NULL);
}

/* --- Settings that need typing ---------------------------------------- */

static void api_key_entered(const char *text)
{
    buddy_settings_t *settings = buddy_settings_get();

    /*
     * Sanitised rather than copied straight in. The touchscreen keyboard has a
     * space bar, and a trailing space is invisible in a password field but
     * makes the Authorization header invalid — producing a 401 that looks
     * exactly like a wrong key.
     */
    char key[BUDDY_CLOUD_API_KEY_MAX_LEN];
    buddy_api_key_sanitise(text, key, sizeof(key));

    memcpy(settings->cloud_api_key, key, strlen(key) + 1);

    /* A key is useless with the feature switched off, so switch it on. */
    if (settings->cloud_api_key[0] != '\0') {
        settings->cloud_brain_enabled = true;
    }
    buddy_settings_save();
    voice_cloud_brain_reset_conversation();

    char description[48];
    buddy_api_key_describe(settings->cloud_api_key, description, sizeof(description));
    ESP_LOGI(TAG, "API key updated: %s", description);

    if (settings->cloud_api_key[0] != '\0' &&
        !buddy_api_key_looks_plausible(settings->cloud_api_key)) {
        ESP_LOGW(TAG, "That is shorter than any key OpenAI issues. A project key "
                      "runs to about 165 characters — check nothing was missed.");
    }
}

static void api_key_row_cb(lv_event_t *event)
{
    (void)event;
    open_text_entry("API key", buddy_settings_get()->cloud_api_key,
                    true, BUDDY_CLOUD_API_KEY_MAX_LEN - 1, api_key_entered);
}

static void personality_entered(const char *text)
{
    buddy_settings_t *settings = buddy_settings_get();
    strncpy(settings->personality_prompt, text, sizeof(settings->personality_prompt) - 1);
    settings->personality_prompt[sizeof(settings->personality_prompt) - 1] = '\0';
    buddy_settings_save();
    voice_cloud_brain_reset_conversation();
}

static void personality_row_cb(lv_event_t *event)
{
    (void)event;
    open_text_entry("Personality", buddy_settings_get()->personality_prompt,
                    false, BUDDY_PERSONALITY_MAX_LEN - 1, personality_entered);
}

static void home_brain_url_entered(const char *text)
{
    buddy_settings_t *settings = buddy_settings_get();
    strncpy(settings->home_brain_url, text, sizeof(settings->home_brain_url) - 1);
    settings->home_brain_url[sizeof(settings->home_brain_url) - 1] = '\0';
    buddy_settings_save();
    voice_cloud_brain_reset_conversation();
}

static void home_brain_url_row_cb(lv_event_t *event)
{
    (void)event;
    open_text_entry("Home AI address", buddy_settings_get()->home_brain_url,
                    false, BUDDY_HOME_BRAIN_URL_MAX_LEN - 1, home_brain_url_entered);
}

static void stream_url_entered(const char *text)
{
    buddy_settings_t *settings = buddy_settings_get();
    strncpy(settings->stream_url, text, sizeof(settings->stream_url) - 1);
    settings->stream_url[sizeof(settings->stream_url) - 1] = '\0';
    buddy_settings_save();
}

static void stream_url_row_cb(lv_event_t *event)
{
    (void)event;
    open_text_entry("Music stream address", buddy_settings_get()->stream_url,
                    false, BUDDY_STREAM_URL_MAX_LEN - 1, stream_url_entered);
}

/* --- Diagnostics -------------------------------------------------------- */

static void diagnostics_dismiss_cb(lv_event_t *event)
{
    lv_obj_t *button_row = lv_event_get_target(event);
    lv_msgbox_close(lv_obj_get_parent(button_row));
}

/**
 * @brief Run the self test and show the result on screen.
 *
 * This is the fastest way to answer "why is it not doing X" without plugging
 * the box into a computer and reading the log.
 */
static void diagnostics_row_cb(lv_event_t *event)
{
    (void)event;

    /*
     * Static, not on the stack. Together these are about three kilobytes in one
     * frame, and this runs as an LVGL event callback on the LVGL task — whose
     * stack is sized for drawing widgets, not for holding a full report and its
     * formatted text at the same time. A stack overflow here would present as a
     * crash on tapping Diagnostics, which is a memorably unhelpful thing for a
     * diagnostics screen to do.
     *
     * Safe as statics because LVGL callbacks all run on that one task, so there
     * is never a second one in flight.
     */
    static buddy_selftest_report_t report;
    static char body[BUDDY_SELFTEST_MAX_CHECKS *
                     (BUDDY_CHECK_NAME_MAX_LEN + BUDDY_CHECK_DETAIL_MAX_LEN)];

    buddy_selftest_run(&report);
    buddy_selftest_format(&report, body, sizeof(body));

    char title[48];
    snprintf(title, sizeof(title), "%u ok, %u notes, %u failed",
             report.passed_count, report.warning_count, report.failed_count);

    static const char *buttons[] = { "Close", "" };

    lv_obj_t *dialog = lv_msgbox_create(NULL, title, body, buttons, false);
    lv_obj_set_width(dialog, BUDDY_SCREEN_WIDTH - 20);
    lv_obj_center(dialog);
    lv_obj_add_event_cb(lv_msgbox_get_btns(dialog), diagnostics_dismiss_cb,
                        LV_EVENT_VALUE_CHANGED, NULL);
}

/* --- Hearing ------------------------------------------------------------ */

/**
 * @brief Show what the buddy currently thinks its room sounds like, and offer
 *        to make it forget.
 *
 * This exists because of a specific failure. The speech gate learns how loud
 * the room is and asks speech to stand out from that; if it learns a figure too
 * high — a fan running while the box booted, a noisy afternoon, the Noisy-room
 * profile on top of an already loud room — it can stop believing anything it
 * hears is speech. From the outside that is simply a box that has gone quiet,
 * with nothing on screen to suggest why or what to do about it.
 *
 * Since v3.7 that can no longer silence the buddy outright (see voice_router.c,
 * where only an inaudible recording may do that), but it can still stop
 * questions reaching the online brain. Two numbers and a button turn an
 * invisible, unexplainable fault into something the owner can see and clear
 * without a computer, a serial cable or a reflash.
 */
static void hearing_dismiss_cb(lv_event_t *event)
{
    lv_obj_t *button_row = lv_event_get_target(event);
    const char *pressed = lv_msgbox_get_active_btn_text(lv_obj_get_parent(button_row));

    if (pressed != NULL && strcmp(pressed, "Forget room") == 0) {
        /*
         * The estimate rebuilds itself within a few seconds of idle audio, so
         * this is not destructive — it is "measure it again, starting now",
         * which is what somebody wants after the noisy thing has stopped.
         */
        voice_speech_gate_forget_room();
        ESP_LOGI(TAG, "Room level forgotten at the owner's request");
    }

    lv_msgbox_close(lv_obj_get_parent(button_row));
}

static void hearing_row_cb(lv_event_t *event)
{
    (void)event;

    static char body[320];

    const uint32_t room      = voice_speech_gate_room_level();
    const uint32_t threshold = voice_speech_gate_threshold();
    const int32_t  peak      = voice_recorder_get_peak_amplitude();

    snprintf(body, sizeof(body),
             "Room background: %lu\n"
             "Speech must reach: %lu\n"
             "Loudest thing you last said: %ld\n"
             "\n"
             "All on a scale to 32767. If the last figure is far below the "
             "second one, the buddy is hearing you but not counting it as "
             "speech. Forget the room and say something again.",
             (unsigned long)room, (unsigned long)threshold, (long)peak);

    static const char *buttons[] = { "Forget room", "Close", "" };

    lv_obj_t *dialog = lv_msgbox_create(NULL, "Hearing", body, buttons, false);
    lv_obj_set_width(dialog, BUDDY_SCREEN_WIDTH - 20);
    lv_obj_center(dialog);
    lv_obj_add_event_cb(lv_msgbox_get_btns(dialog), hearing_dismiss_cb,
                        LV_EVENT_VALUE_CHANGED, NULL);
}

/* --- Factory reset ----------------------------------------------------- */

/**
 * @brief A button in the confirmation dialog was pressed.
 *
 * The event arrives from the dialog's button matrix, so the text of the button
 * that was actually pressed has to be checked — otherwise "Cancel" would wipe
 * the settings just as happily as "Erase".
 */
static void factory_reset_confirmed_cb(lv_event_t *event)
{
    lv_obj_t *button_row = lv_event_get_target(event);
    lv_obj_t *dialog = lv_obj_get_parent(button_row);

    const char *pressed = lv_msgbox_get_active_btn_text(dialog);

    if (pressed != NULL && strcmp(pressed, "Erase") == 0) {
        ESP_LOGW(TAG, "Factory reset confirmed — restarting");
        buddy_settings_reset_to_defaults();
        buddy_settings_save();
        esp_restart();
    }

    lv_msgbox_close(dialog);
}

static void factory_reset_row_cb(lv_event_t *event)
{
    (void)event;

    /* A deliberate two-step, because this wipes the Wi-Fi and the API key. */
    static const char *buttons[] = { "Cancel", "Erase", "" };

    lv_obj_t *dialog = lv_msgbox_create(NULL, "Reset everything?",
                                        "Wi-Fi details, API key and all preferences "
                                        "will be erased and the buddy will restart.",
                                        buttons, false);
    lv_obj_center(dialog);

    lv_obj_add_event_cb(lv_msgbox_get_btns(dialog), factory_reset_confirmed_cb,
                        LV_EVENT_VALUE_CHANGED, NULL);
}

/* ------------------------------------------------------------------------ */
/* The Wi-Fi page                                                            */
/* ------------------------------------------------------------------------ */

static void wifi_page_back_cb(lv_event_t *event)
{
    (void)event;
    if (s_wifi_status_timer != NULL) {
        lv_timer_del(s_wifi_status_timer);
        s_wifi_status_timer = NULL;
    }
    if (s_wifi_page != NULL) {
        lv_obj_del(s_wifi_page);
        s_wifi_page = NULL;
        s_wifi_network_list = NULL;
        s_wifi_status_label = NULL;
    }

    /*
     * Repaint the row underneath NOW rather than up to a second from now. This
     * is the exact moment the stale "Not connected" used to be revealed.
     */
    main_page_refresh_cb(NULL);
}

static void wifi_password_entered(const char *password)
{
    ESP_LOGI(TAG, "Joining \"%s\"", s_pending_wifi_ssid);
    net_wifi_connect(s_pending_wifi_ssid, password);

    /* The poller takes it from here and reports whatever actually happens. */
    wifi_status_timer_cb(NULL);
}

/**
 * @brief A network in the list was tapped.
 */
static void wifi_network_chosen_cb(lv_event_t *event)
{
    lv_obj_t *button = lv_event_get_target(event);
    const char *ssid = lv_list_get_btn_text(s_wifi_network_list, button);

    if (ssid == NULL) {
        return;
    }

    strncpy(s_pending_wifi_ssid, ssid, sizeof(s_pending_wifi_ssid) - 1);
    s_pending_wifi_ssid[sizeof(s_pending_wifi_ssid) - 1] = '\0';

    open_text_entry(s_pending_wifi_ssid, "", true,
                    BUDDY_WIFI_PASSWORD_MAX_LEN - 1, wifi_password_entered);
}

/**
 * @brief Scan results arrived — fill the list in.
 *
 * @note Runs on the scan task, so the display lock has to be taken here.
 */
static void wifi_scan_finished(const net_wifi_network_t *networks, size_t count)
{
    if (!hw_display_lock(1000)) {
        return;
    }

    if (s_wifi_network_list != NULL) {
        lv_obj_clean(s_wifi_network_list);

        for (size_t i = 0; i < count; i++) {
            lv_obj_t *button = lv_list_add_btn(
                s_wifi_network_list,
                networks[i].needs_password ? LV_SYMBOL_WIFI : LV_SYMBOL_OK,
                networks[i].ssid);
            lv_obj_add_event_cb(button, wifi_network_chosen_cb, LV_EVENT_CLICKED, NULL);
        }

        if (count == 0) {
            lv_list_add_text(s_wifi_network_list, "No networks found");
        }
    }

    if (s_wifi_status_label != NULL) {
        lv_label_set_text_fmt(s_wifi_status_label, "%u networks nearby", (unsigned)count);
    }

    hw_display_unlock();
}

static void wifi_rescan_cb(lv_event_t *event)
{
    (void)event;

    if (s_wifi_status_label != NULL) {
        lv_label_set_text(s_wifi_status_label, "Scanning...");
    }
    net_wifi_scan_async(wifi_scan_finished);
}

/**
 * @brief Repaint the Wi-Fi status line from the live connection state.
 *
 * This exists because the page used to set "Connecting to X..." once, when the
 * password was entered, and then had no way on earth to change it again. The
 * connection state lives in net_wifi.c and its single state callback is already
 * spoken for by main.c, which uses it to start the clock. So the label sat at
 * "Connecting..." for ever — whether the attempt failed, or in fact SUCCEEDED.
 * From the outside those are indistinguishable, and both look like a box stuck
 * in a loop.
 *
 * Polling twice a second is the cheap fix: no callback plumbing, no second
 * subscriber to arrange, and the page owns the timer so it dies with the page.
 */
static void wifi_status_timer_cb(lv_timer_t *timer)
{
    (void)timer;

    if (s_wifi_status_label == NULL) {
        return;
    }

    switch (net_wifi_get_state()) {
    case NET_WIFI_CONNECTED:
        lv_label_set_text_fmt(s_wifi_status_label, LV_SYMBOL_OK " Connected to %s\n%s",
                              net_wifi_get_ssid(), net_wifi_get_ip_address());
        lv_obj_set_style_text_color(s_wifi_status_label, FACE_COLOR_TEXT, LV_PART_MAIN);
        break;

    case NET_WIFI_CONNECTING:
        /*
         * Two different waits, and saying which one saves a lot of guessing:
         * before association the password is still unproven, after it the
         * router has accepted us and DHCP is the thing taking its time.
         */
        if (net_wifi_is_associated()) {
            lv_label_set_text_fmt(s_wifi_status_label,
                                  "Joined %s. Waiting for an address...",
                                  net_wifi_get_ssid());
        } else {
            lv_label_set_text_fmt(s_wifi_status_label, "Connecting to %s...",
                                  net_wifi_get_ssid());
        }
        lv_obj_set_style_text_color(s_wifi_status_label, FACE_COLOR_TEXT_MUTED, LV_PART_MAIN);
        break;

    case NET_WIFI_FAILED:
        lv_label_set_text_fmt(s_wifi_status_label, "%s\nTap the network to try again.",
                              net_wifi_get_error());
        lv_obj_set_style_text_color(s_wifi_status_label, FACE_COLOR_ESPRESSIF_RED, LV_PART_MAIN);
        break;

    case NET_WIFI_IDLE:
    default:
        if (s_wifi_network_list != NULL &&
            lv_obj_get_child_cnt(s_wifi_network_list) == 0) {
            lv_label_set_text(s_wifi_status_label, "Scanning...");
        }
        lv_obj_set_style_text_color(s_wifi_status_label, FACE_COLOR_TEXT_MUTED, LV_PART_MAIN);
        break;
    }
}

static void open_wifi_page(void)
{
    lv_obj_t *body = NULL;
    s_wifi_page = create_page("Wi-Fi", wifi_page_back_cb, &body);

    s_wifi_status_label = lv_label_create(body);
    lv_obj_set_style_text_color(s_wifi_status_label, FACE_COLOR_TEXT_MUTED, LV_PART_MAIN);

    if (net_wifi_is_connected()) {
        lv_label_set_text_fmt(s_wifi_status_label, "Connected to %s (%s)",
                              net_wifi_get_ssid(), net_wifi_get_ip_address());
    } else {
        lv_label_set_text(s_wifi_status_label, "Scanning...");
    }

    lv_obj_t *rescan = lv_btn_create(body);
    lv_obj_set_size(rescan, lv_pct(100), 38);
    lv_obj_set_style_bg_color(rescan, FACE_COLOR_ESPRESSIF_RED, LV_PART_MAIN);
    lv_obj_add_event_cb(rescan, wifi_rescan_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *rescan_label = lv_label_create(rescan);
    lv_label_set_text(rescan_label, LV_SYMBOL_REFRESH " Scan again");
    lv_obj_center(rescan_label);

    s_wifi_network_list = lv_list_create(body);
    lv_obj_set_size(s_wifi_network_list, lv_pct(100), 130);

    s_wifi_status_timer = lv_timer_create(wifi_status_timer_cb, 500, NULL);
    wifi_status_timer_cb(NULL);

    net_wifi_scan_async(wifi_scan_finished);
}

static void wifi_row_cb(lv_event_t *event)
{
    (void)event;
    open_wifi_page();
}

/* ------------------------------------------------------------------------ */
/* The main settings page                                                    */
/* ------------------------------------------------------------------------ */

static void main_page_back_cb(lv_event_t *event)
{
    (void)event;
    ui_settings_close();
}

/**
 * @brief Add a row whose control is an on/off switch.
 */
static void add_switch_row(lv_obj_t *body, const char *label, bool initial_state,
                           lv_event_cb_t on_change)
{
    lv_obj_t *row = create_row(body, label);

    lv_obj_t *toggle = lv_switch_create(row);
    lv_obj_align(toggle, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_bg_color(toggle, FACE_COLOR_ESPRESSIF_RED, LV_PART_INDICATOR | LV_STATE_CHECKED);
    if (initial_state) {
        lv_obj_add_state(toggle, LV_STATE_CHECKED);
    }
    lv_obj_add_event_cb(toggle, on_change, LV_EVENT_VALUE_CHANGED, NULL);
}

/**
 * @brief Add a row whose control is a slider.
 */
static void add_slider_row(lv_obj_t *body, const char *label, int minimum, int maximum,
                           int initial_value, lv_event_cb_t on_change)
{
    lv_obj_t *row = create_row(body, label);

    lv_obj_t *slider = lv_slider_create(row);
    lv_obj_set_width(slider, 130);
    lv_obj_align(slider, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_slider_set_range(slider, minimum, maximum);
    lv_slider_set_value(slider, initial_value, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(slider, FACE_COLOR_ESPRESSIF_RED, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(slider, FACE_COLOR_ESPRESSIF_RED, LV_PART_KNOB);
    lv_obj_add_event_cb(slider, on_change, LV_EVENT_VALUE_CHANGED, NULL);
}

/**
 * @brief Add a row that opens another page when tapped.
 */
/* ------------------------------------------------------------------------ */
/* The Question Examples page                                                */
/* ------------------------------------------------------------------------ */

/**
 * @brief A wrapping paragraph of muted explanatory text.
 *
 * Rows are for things you can change; this is for things you need to know.
 */
static void create_note(lv_obj_t *body, const char *text)
{
    lv_obj_t *note = lv_label_create(body);
    lv_label_set_text(note, text);
    lv_label_set_long_mode(note, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(note, BUDDY_SCREEN_WIDTH - 24);
    lv_obj_set_style_text_color(note, FACE_COLOR_TEXT_MUTED, LV_PART_MAIN);
    lv_obj_set_style_text_font(note, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_style_pad_top(note, 6, LV_PART_MAIN);
    lv_obj_set_style_pad_left(note, 4, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(note, 2, LV_PART_MAIN);
}

/**
 * @brief One example phrase, shown in quotes so it reads as something to say.
 */
static void create_phrase_row(lv_obj_t *body, const char *phrase)
{
    char quoted[VOICE_PHRASE_MAX_LEN + 4];
    snprintf(quoted, sizeof(quoted), "\"%s\"", phrase);

    lv_obj_t *row = create_row(body, quoted);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_CLICKABLE);
}

static void examples_page_back_cb(lv_event_t *event)
{
    (void)event;
    if (s_examples_page != NULL) {
        lv_obj_del(s_examples_page);
        s_examples_page = NULL;
    }
    if (s_expressions_page != NULL) {
        lv_obj_del(s_expressions_page);
        s_expressions_page = NULL;
    }
}

/**
 * @brief Build the list of everything the buddy understands with no internet.
 *
 * The whole page is generated by walking the vocabulary table in voice_commands.c
 * — the same table that gets loaded into the recogniser. That is deliberate: a
 * hand-written list would quietly rot every time a phrase was added or reworded,
 * and a list of phrases that do not work is worse than no list at all.
 */
static void open_examples_page(void)
{
    const buddy_language_t language = buddy_settings_get()->language;

    lv_obj_t *body = NULL;
    s_examples_page = create_page("Question Examples", examples_page_back_cb, &body);

    char intro[128];
    snprintf(intro, sizeof(intro),
             "Say \"%s\", wait for the chirp, then say one of these.",
             (language == BUDDY_LANGUAGE_CHINESE) ? BUDDY_WAKE_WORD_CHINESE
                                                  : BUDDY_WAKE_WORD_ENGLISH);
    create_note(body, intro);

    /*
     * Walk the actions in order rather than the table in order, so phrases that
     * do the same thing are grouped together under one heading no matter where
     * they happen to sit in the table.
     */
    for (int action = 0; action < VOICE_ACTION_COUNT; action++) {
        bool heading_written = false;

        for (uint16_t i = 0; i < voice_commands_get_count(); i++) {
            const voice_command_t *command = voice_commands_get(i);
            if (command == NULL ||
                command->language != language ||
                command->action != (voice_action_t)action) {
                continue;
            }

            if (!heading_written) {
                create_section_heading(body,
                                       voice_commands_action_title((voice_action_t)action));
                heading_written = true;
            }
            create_phrase_row(body, command->phrase);
        }
    }

    create_note(body,
                "These all work with the Wi-Fi unplugged. Anything else you ask "
                "goes to the online brain instead, which needs Wi-Fi and an API "
                "key — except sums, which are always worked out here.");
}

static void examples_row_cb(lv_event_t *event)
{
    (void)event;
    open_examples_page();
}

/* ------------------------------------------------------------------------ */
/* The Face Expressions page                                                 */
/* ------------------------------------------------------------------------ */

/**
 * @brief Find the phrase that asks for an expression, in the current language.
 *
 * @return The phrase, or NULL when nothing spoken triggers this expression.
 */
static const char *phrase_for_emotion(face_emotion_t emotion, buddy_language_t language)
{
    const voice_action_t action = voice_local_brain_action_for_emotion(emotion);
    if (action == VOICE_ACTION_NONE) {
        return NULL;
    }

    for (uint16_t i = 0; i < voice_commands_get_count(); i++) {
        const voice_command_t *command = voice_commands_get(i);
        if (command != NULL &&
            command->language == language &&
            command->action == action) {
            return command->phrase;
        }
    }
    return NULL;
}

/**
 * @brief Tapping an expression: close Settings and pull that face.
 *
 * Closing is the point — the whole screen is the face, so there is nowhere to
 * preview an expression except by getting out of the way and showing it.
 */
static void expression_row_cb(lv_event_t *event)
{
    const face_emotion_t emotion =
        (face_emotion_t)(intptr_t)lv_event_get_user_data(event);

    ui_settings_close();
    face_animator_flash_emotion(emotion, EXPRESSION_PREVIEW_MS);
}

static void expressions_page_back_cb(lv_event_t *event)
{
    (void)event;
    if (s_expressions_page != NULL) {
        lv_obj_del(s_expressions_page);
        s_expressions_page = NULL;
    }
}

/**
 * @brief Every face the buddy owns, with the words that summon it.
 *
 * Generated by walking the expression table, so a newly added mood turns up
 * here on its own rather than needing to be remembered twice.
 */
static void open_expressions_page(void)
{
    const buddy_language_t language = buddy_settings_get()->language;

    lv_obj_t *body = NULL;
    s_expressions_page = create_page("Face Expressions", expressions_page_back_cb, &body);

    create_note(body, "Tap any face to see it. The ones with phrases can also be "
                      "asked for out loud, after the wake word.");

    bool requestable_heading_written = false;
    bool automatic_heading_written = false;

    /* Two passes: the ones you can ask for, then the ones it uses by itself. */
    for (int pass = 0; pass < 2; pass++) {
        for (int emotion = 0; emotion < FACE_EMOTION_COUNT; emotion++) {
            const char *phrase = phrase_for_emotion((face_emotion_t)emotion, language);
            const bool requestable = (phrase != NULL);

            if ((pass == 0) != requestable) {
                continue;
            }

            if (pass == 0 && !requestable_heading_written) {
                create_section_heading(body, "ASK FOR THESE");
                requestable_heading_written = true;
            }
            if (pass == 1 && !automatic_heading_written) {
                create_section_heading(body, "IT USES THESE ON ITS OWN");
                automatic_heading_written = true;
            }

            /* The stored names are lowercase; capitalise for display. */
            char label[32];
            snprintf(label, sizeof(label), "%s", face_emotion_get_name((face_emotion_t)emotion));
            if (label[0] >= 'a' && label[0] <= 'z') {
                label[0] = (char)(label[0] - 'a' + 'A');
            }

            lv_obj_t *row = create_row(body, label);
            lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_add_event_cb(row, expression_row_cb, LV_EVENT_CLICKED,
                                (void *)(intptr_t)emotion);

            char detail[VOICE_PHRASE_MAX_LEN + 4];
            if (requestable) {
                snprintf(detail, sizeof(detail), "\"%s\"", phrase);
            } else {
                snprintf(detail, sizeof(detail), "automatic");
            }

            lv_obj_t *detail_label = lv_label_create(row);
            lv_label_set_text(detail_label, detail);
            lv_obj_set_style_text_color(detail_label, FACE_COLOR_TEXT_MUTED, LV_PART_MAIN);
            lv_obj_set_style_text_font(detail_label, &lv_font_montserrat_14, LV_PART_MAIN);
            lv_label_set_long_mode(detail_label, LV_LABEL_LONG_DOT);
            lv_obj_set_width(detail_label, 168);
            lv_obj_set_style_text_align(detail_label, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
            lv_obj_align(detail_label, LV_ALIGN_RIGHT_MID, 0, 0);
        }
    }
}

static void expressions_row_cb(lv_event_t *event)
{
    (void)event;
    open_expressions_page();
}

/**
 * @brief Add a row that opens another page, with a value shown on the right.
 *
 * @return The value label, so a caller whose value can CHANGE while the page is
 *         open can keep hold of it and repaint it. Ignoring the return is fine
 *         for the rows whose value only changes by leaving and coming back.
 */
static lv_obj_t *add_navigation_row(lv_obj_t *body, const char *label, const char *value,
                                    lv_event_cb_t on_click)
{
    lv_obj_t *row = create_row(body, label);
    lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(row, on_click, LV_EVENT_CLICKED, NULL);

    lv_obj_t *value_label = lv_label_create(row);
    lv_label_set_text(value_label, (value != NULL && value[0] != '\0') ? value : "Set");
    lv_obj_set_style_text_color(value_label, FACE_COLOR_TEXT_MUTED, LV_PART_MAIN);
    lv_label_set_long_mode(value_label, LV_LABEL_LONG_DOT);
    lv_obj_set_width(value_label, 130);
    lv_obj_set_style_text_align(value_label, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
    lv_obj_align(value_label, LV_ALIGN_RIGHT_MID, -14, 0);

    lv_obj_t *chevron = lv_label_create(row);
    lv_label_set_text(chevron, LV_SYMBOL_RIGHT);
    lv_obj_set_style_text_color(chevron, FACE_COLOR_TEXT_MUTED, LV_PART_MAIN);
    lv_obj_align(chevron, LV_ALIGN_RIGHT_MID, 0, 0);

    return value_label;
}

/**
 * @brief A row that only reports something, with nothing to tap.
 *
 * @return The value label, so a timer can keep it current.
 */
static lv_obj_t *add_value_row(lv_obj_t *body, const char *label, const char *value)
{
    lv_obj_t *row = create_row(body, label);

    lv_obj_t *value_label = lv_label_create(row);
    lv_label_set_text(value_label, (value != NULL) ? value : "");
    lv_obj_set_style_text_color(value_label, FACE_COLOR_TEXT_MUTED, LV_PART_MAIN);
    lv_obj_align(value_label, LV_ALIGN_RIGHT_MID, 0, 0);

    return value_label;
}

/**
 * @brief Keep the main page's live values honest.
 *
 * The Wi-Fi row used to be written once, when the page was built, from
 * whatever the connection state happened to be at that instant. Joining a
 * network from the sub-page and backing out did not rebuild anything, so the
 * row underneath still said "Not connected" about a box that was very much
 * connected — and the only way to clear it was to close Settings and reopen.
 *
 * Anything on this page that can change while it is open belongs here.
 */
static void main_page_refresh_cb(lv_timer_t *timer)
{
    (void)timer;

    if (s_main_page == NULL) {
        return;
    }

    if (s_wifi_row_value != NULL) {
        const bool connected = net_wifi_is_connected();

        lv_label_set_text(s_wifi_row_value,
                          connected ? net_wifi_get_ssid()
                                    : (net_wifi_get_state() == NET_WIFI_CONNECTING)
                                          ? "Connecting..."
                                          : "Not connected");
        lv_obj_set_style_text_color(s_wifi_row_value,
                                    connected ? FACE_COLOR_TEXT : FACE_COLOR_TEXT_MUTED,
                                    LV_PART_MAIN);
    }

    if (s_api_key_row_value != NULL) {
        const char *key = buddy_settings_get()->cloud_api_key;

        if (key[0] == '\0') {
            lv_label_set_text(s_api_key_row_value, "Not set");
            lv_obj_set_style_text_color(s_api_key_row_value, FACE_COLOR_TEXT_MUTED,
                                        LV_PART_MAIN);
        } else {
            /*
             * The character count, not the word "Saved".
             *
             * A key that lost its tail is stored exactly as happily as a whole
             * one, and "Saved" reports both identically — which is how a
             * truncated key can sit here looking correct while every request
             * comes back 401. The number can be compared against what OpenAI
             * showed you: a project key runs to about 165 characters.
             */
            lv_label_set_text_fmt(s_api_key_row_value, "%u chars", (unsigned)strlen(key));
            lv_obj_set_style_text_color(s_api_key_row_value,
                                        buddy_api_key_looks_plausible(key)
                                            ? FACE_COLOR_TEXT : FACE_COLOR_ESPRESSIF_RED,
                                        LV_PART_MAIN);
        }
    }

    if (s_home_url_row_value != NULL) {
        const char *url = buddy_settings_get()->home_brain_url;
        lv_label_set_text(s_home_url_row_value, (url[0] != '\0') ? url : "Not set");
        lv_obj_set_style_text_color(s_home_url_row_value,
                                    (url[0] != '\0') ? FACE_COLOR_TEXT : FACE_COLOR_TEXT_MUTED,
                                    LV_PART_MAIN);
    }

    if (s_requests_row_value != NULL) {
        const uint16_t left = voice_assistant_requests_remaining();

        if (!buddy_settings_get()->cloud_brain_enabled) {
            lv_label_set_text(s_requests_row_value, "not in use");
        } else if (left == UINT16_MAX) {
            lv_label_set_text(s_requests_row_value, "no limit");
        } else {
            lv_label_set_text_fmt(s_requests_row_value, "%u of %u", left,
                                  buddy_settings_get()->requests_per_hour);
        }
    }

    if (s_about_label != NULL) {
        lv_label_set_text_fmt(s_about_label, "%s %s\nIP: %s",
                              BUDDY_FIRMWARE_NAME, BUDDY_FIRMWARE_VERSION,
                              (net_wifi_get_ip_address()[0] != '\0')
                                  ? net_wifi_get_ip_address()
                                  : "offline");
    }
}

static void build_main_page(void)
{
    const buddy_settings_t *settings = buddy_settings_get();

    lv_obj_t *body = NULL;
    s_main_page = create_page("Settings", main_page_back_cb, &body);

    /*
     * The three rows a box fresh out of the packaging actually needs, before
     * any of the fine-tuning. Question examples comes first because knowing
     * what to say is the difference between a working buddy and a silent one —
     * a list of phrases buried five sections down helps nobody.
     */
    create_section_heading(body, "START HERE");
    add_navigation_row(body, "Question examples", "What to say", examples_row_cb);
    add_navigation_row(body, "Calculator", "Any sum, offline", calculator_row_cb);
    s_wifi_row_value = add_navigation_row(body, "Wi-Fi", "Not connected", wifi_row_cb);

    lv_obj_t *start_note = lv_label_create(body);
    lv_label_set_text(start_note,
                      "Wi-Fi is only needed for the clock. Every phrase in the "
                      "examples list, the faces, the timer, the sensors and the "
                      "music all work with no network at all.");
    lv_label_set_long_mode(start_note, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(start_note, BUDDY_SCREEN_WIDTH - 24);
    lv_obj_set_style_text_color(start_note, FACE_COLOR_TEXT_MUTED, LV_PART_MAIN);
    lv_obj_set_style_text_font(start_note, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_style_pad_left(start_note, 4, LV_PART_MAIN);
    lv_obj_set_style_pad_top(start_note, 2, LV_PART_MAIN);

    /* --- Sound --------------------------------------------------------- */
    create_section_heading(body, "SOUND");
    add_slider_row(body, "Volume", 0, 100, settings->speaker_volume, volume_changed_cb);
    add_switch_row(body, "Chimes", settings->chimes_enabled, chimes_toggled_cb);

    /* --- Screen and personality ---------------------------------------- */
    create_section_heading(body, "FACE");
    add_slider_row(body, "Brightness", 10, 100, settings->screen_brightness,
                   brightness_changed_cb);
    add_switch_row(body, "Idle antics", settings->idle_antics_enabled, idle_antics_toggled_cb);
    add_switch_row(body, "React to touch", settings->touch_antics_enabled,
                   touch_antics_toggled_cb);
    add_navigation_row(body, "Face expressions", "Tap to try", expressions_row_cb);

    /* --- Sensors -------------------------------------------------------- */
    create_section_heading(body, "SENSORS");
    add_switch_row(body, "Show readout", settings->show_sensor_readout,
                   show_readout_toggled_cb);
    add_switch_row(body, "Fahrenheit",
                   settings->temperature_unit == BUDDY_TEMPERATURE_FAHRENHEIT,
                   fahrenheit_toggled_cb);

    if (!hw_sensors_is_dock_present()) {
        lv_obj_t *note = lv_label_create(body);
        lv_label_set_text(note, "No sensor dock attached.");
        lv_obj_set_style_text_color(note, FACE_COLOR_TEXT_MUTED, LV_PART_MAIN);
        lv_obj_set_style_pad_left(note, 4, LV_PART_MAIN);
    }

    /* --- Voice ----------------------------------------------------------- */
    create_section_heading(body, "VOICE");

    lv_obj_t *language_row = create_row(body, "Language");
    lv_obj_t *language_dropdown = lv_dropdown_create(language_row);
    lv_obj_set_width(language_dropdown, 148);
    lv_obj_align(language_dropdown, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_dropdown_set_options(language_dropdown, "English (Hi E.S.P.)\nChinese (Hi Lexin)");
    lv_dropdown_set_selected(language_dropdown,
                             (settings->language == BUDDY_LANGUAGE_CHINESE) ? 1 : 0);
    lv_obj_add_event_cb(language_dropdown, language_changed_cb, LV_EVENT_VALUE_CHANGED, NULL);

    /*
     * How much racket to expect. The buddy measures its own room continuously
     * and adapts, so Automatic is right almost everywhere — these two exist for
     * the rooms where "almost" is doing real work.
     */
    lv_obj_t *noise_row = create_row(body, "Room noise");
    lv_obj_t *noise_dropdown = lv_dropdown_create(noise_row);
    lv_obj_set_width(noise_dropdown, 148);
    lv_obj_align(noise_dropdown, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_dropdown_set_options(noise_dropdown, "Automatic\nQuiet room\nNoisy room");
    lv_dropdown_set_selected(noise_dropdown, (uint16_t)settings->noise_profile);
    lv_obj_add_event_cb(noise_dropdown, noise_profile_changed_cb, LV_EVENT_VALUE_CHANGED, NULL);

    /* --- The online brain ------------------------------------------------ */
    /*
     * Down here, not under START HERE, and off unless somebody turns it on.
     *
     * This section is the only part of the buddy that costs money to use, and
     * it is worth being straight about that rather than presenting it as step
     * three of setting the box up. Everything above this line runs on the chip.
     */
    create_section_heading(body, "ONLINE BRAIN (OPTIONAL)");
    add_switch_row(body, "Answer any question", settings->cloud_brain_enabled,
                   cloud_toggled_cb);

    /*
     * How eagerly, not just whether. The switch above is the master; this
     * decides what the buddy does with a question once it is allowed to.
     */
    lv_obj_t *mode_row = create_row(body, "When to ask");
    lv_obj_t *mode_dropdown = lv_dropdown_create(mode_row);
    lv_obj_set_width(mode_dropdown, 148);
    lv_obj_align(mode_dropdown, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_dropdown_set_options(mode_dropdown, "Never\nOnly when stuck\nAlways\nHome AI");
    lv_dropdown_set_selected(mode_dropdown, (uint16_t)settings->brain_mode);
    lv_obj_add_event_cb(mode_dropdown, brain_mode_changed_cb, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t *limit_row = create_row(body, "Limit per hour");
    lv_obj_t *limit_dropdown = lv_dropdown_create(limit_row);
    lv_obj_set_width(limit_dropdown, 148);
    lv_obj_align(limit_dropdown, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_dropdown_set_options(limit_dropdown, "10\n30\n60\n120\nNo limit");

    uint16_t limit_index = 1;   /* 30, the default. */
    for (size_t i = 0; i < sizeof(s_request_limits) / sizeof(s_request_limits[0]); i++) {
        if (s_request_limits[i] == settings->requests_per_hour) {
            limit_index = (uint16_t)i;
            break;
        }
    }
    lv_dropdown_set_selected(limit_dropdown, limit_index);
    lv_obj_add_event_cb(limit_dropdown, request_limit_changed_cb, LV_EVENT_VALUE_CHANGED, NULL);

    s_requests_row_value = add_value_row(body, "Left this hour", "-");
    s_api_key_row_value = add_navigation_row(body, "API key", "Not set", api_key_row_cb);
    add_navigation_row(body, "Personality", settings->personality_prompt, personality_row_cb);

    lv_obj_t *cloud_note = lv_label_create(body);
    lv_label_set_text(cloud_note,
                      "Off by default. Switching this on sends questions it does "
                      "not recognise to OpenAI, which bills your account per "
                      "question.\n\n"
                      "Only when stuck is the mode to want: it answers on the chip "
                      "wherever it can and pays only for what it genuinely cannot "
                      "do. The hourly limit is what stops a misheard wake word in "
                      "a noisy room running up a bill overnight.");
    lv_label_set_long_mode(cloud_note, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(cloud_note, BUDDY_SCREEN_WIDTH - 24);
    lv_obj_set_style_text_color(cloud_note, FACE_COLOR_TEXT_MUTED, LV_PART_MAIN);
    lv_obj_set_style_text_font(cloud_note, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_style_pad_left(cloud_note, 4, LV_PART_MAIN);
    lv_obj_set_style_pad_top(cloud_note, 2, LV_PART_MAIN);

    /* --- The home brain ---------------------------------------------------- */
    /*
     * Its own heading, because it is not the online brain: no account, no key,
     * no bill. It is chosen with "Home AI" in the dropdown above and needs only
     * an address, which home_brain.py prints when it starts.
     */
    create_section_heading(body, "HOME AI (FREE, ON YOUR PC)");
    s_home_url_row_value = add_navigation_row(body, "Home AI address", "Not set",
                                              home_brain_url_row_cb);

    lv_obj_t *home_note = lv_label_create(body);
    lv_label_set_text(home_note,
                      "Pick Home AI under When to ask, then type the address "
                      "home_brain.py shows, like 192.168.1.50:8765. Questions go "
                      "to a small AI on your own computer, never to OpenAI, and "
                      "cost nothing. If that computer is off, the buddy answers "
                      "what it can by itself.");
    lv_label_set_long_mode(home_note, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(home_note, BUDDY_SCREEN_WIDTH - 24);
    lv_obj_set_style_text_color(home_note, FACE_COLOR_TEXT_MUTED, LV_PART_MAIN);
    lv_obj_set_style_text_font(home_note, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_style_pad_left(home_note, 4, LV_PART_MAIN);
    lv_obj_set_style_pad_top(home_note, 2, LV_PART_MAIN);

    /* --- Music ----------------------------------------------------------- */
    create_section_heading(body, "MUSIC");

    lv_obj_t *source_row = create_row(body, "Source");
    lv_obj_t *source_dropdown = lv_dropdown_create(source_row);
    lv_obj_set_width(source_dropdown, 130);
    lv_obj_align(source_dropdown, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_dropdown_set_options(source_dropdown, "Flash\nWi-Fi stream");
    lv_dropdown_set_selected(source_dropdown,
                             (settings->music_source == BUDDY_MUSIC_SOURCE_STREAM) ? 1 : 0);
    lv_obj_add_event_cb(source_dropdown, music_source_changed_cb, LV_EVENT_VALUE_CHANGED, NULL);

    add_navigation_row(body, "Stream address", settings->stream_url, stream_url_row_cb);

    char track_summary[48];
    snprintf(track_summary, sizeof(track_summary), "%u track(s) on flash",
             (unsigned)audio_music_get_track_count());
    lv_obj_t *track_note = lv_label_create(body);
    lv_label_set_text(track_note, track_summary);
    lv_obj_set_style_text_color(track_note, FACE_COLOR_TEXT_MUTED, LV_PART_MAIN);
    lv_obj_set_style_pad_left(track_note, 4, LV_PART_MAIN);

    /* --- About and reset -------------------------------------------------- */
    create_section_heading(body, "ABOUT");

    char about_text[128];
    snprintf(about_text, sizeof(about_text), "%s %s\nIP: %s",
             BUDDY_FIRMWARE_NAME, BUDDY_FIRMWARE_VERSION,
             (net_wifi_get_ip_address()[0] != '\0') ? net_wifi_get_ip_address() : "offline");

    lv_obj_t *diagnostics_row = create_row(body, "Run diagnostics");
    lv_obj_add_flag(diagnostics_row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(diagnostics_row, diagnostics_row_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *diagnostics_icon = lv_label_create(diagnostics_row);
    lv_label_set_text(diagnostics_icon, LV_SYMBOL_RIGHT);
    lv_obj_set_style_text_color(diagnostics_icon, FACE_COLOR_TEXT_MUTED, LV_PART_MAIN);
    lv_obj_align(diagnostics_icon, LV_ALIGN_RIGHT_MID, 0, 0);

    /* Next to Diagnostics, because "it stopped answering me" leads to both. */
    lv_obj_t *hearing_row = create_row(body, "Hearing");
    lv_obj_add_flag(hearing_row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(hearing_row, hearing_row_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *hearing_icon = lv_label_create(hearing_row);
    lv_label_set_text(hearing_icon, LV_SYMBOL_RIGHT);
    lv_obj_set_style_text_color(hearing_icon, FACE_COLOR_TEXT_MUTED, LV_PART_MAIN);
    lv_obj_align(hearing_icon, LV_ALIGN_RIGHT_MID, 0, 0);

    s_about_label = lv_label_create(body);
    lv_label_set_text(s_about_label, about_text);
    lv_obj_set_style_text_color(s_about_label, FACE_COLOR_TEXT_MUTED, LV_PART_MAIN);
    lv_obj_set_style_pad_left(s_about_label, 4, LV_PART_MAIN);

    lv_obj_t *reset_row = create_row(body, "Factory reset");
    lv_obj_add_flag(reset_row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(reset_row, factory_reset_row_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *reset_icon = lv_label_create(reset_row);
    lv_label_set_text(reset_icon, LV_SYMBOL_TRASH);
    lv_obj_set_style_text_color(reset_icon, FACE_COLOR_ESPRESSIF_RED, LV_PART_MAIN);
    lv_obj_align(reset_icon, LV_ALIGN_RIGHT_MID, 0, 0);

    /*
     * Rebuilt pages get a fresh timer, so drop any left from a previous one
     * rather than accumulating a new one on every open.
     */
    if (s_main_page_timer != NULL) {
        lv_timer_del(s_main_page_timer);
    }
    s_main_page_timer = lv_timer_create(main_page_refresh_cb, 1000, NULL);
    main_page_refresh_cb(NULL);
}

/* ------------------------------------------------------------------------ */
/* Public API                                                                */
/* ------------------------------------------------------------------------ */

static void gear_button_cb(lv_event_t *event)
{
    (void)event;
    ui_settings_open();
}

esp_err_t ui_settings_create_button(lv_obj_t *parent)
{
    if (parent == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_gear_button != NULL) {
        return ESP_OK;
    }

    s_gear_button = lv_btn_create(parent);
    lv_obj_remove_style_all(s_gear_button);
    lv_obj_set_size(s_gear_button, BUDDY_SETTINGS_BUTTON_SIZE, BUDDY_SETTINGS_BUTTON_SIZE);
    lv_obj_align(s_gear_button, LV_ALIGN_BOTTOM_LEFT,
                 BUDDY_SCREEN_EDGE_MARGIN, -BUDDY_SCREEN_EDGE_MARGIN);

    lv_obj_set_style_radius(s_gear_button, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_gear_button, FACE_COLOR_SURFACE, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_gear_button, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_gear_button, FACE_COLOR_SURFACE_BORDER,
                              LV_PART_MAIN | LV_STATE_PRESSED);

    lv_obj_t *icon = lv_label_create(s_gear_button);
    lv_label_set_text(icon, LV_SYMBOL_SETTINGS);
    lv_obj_set_style_text_color(icon, FACE_COLOR_ESPRESSIF_RED, LV_PART_MAIN);
    lv_obj_center(icon);

    lv_obj_add_event_cb(s_gear_button, gear_button_cb, LV_EVENT_CLICKED, NULL);

    ESP_LOGI(TAG, "Settings button created");
    return ESP_OK;
}

void ui_settings_open(void)
{
    if (s_main_page != NULL) {
        return;
    }
    ESP_LOGI(TAG, "Opening settings");
    build_main_page();
}

void ui_settings_open_examples(void)
{
    /*
     * Build the main page underneath first. Without it, the back arrow on the
     * examples screen would drop the user onto the bare face with no way back
     * into Settings at all.
     */
    if (s_main_page == NULL) {
        build_main_page();
    }
    if (s_examples_page == NULL) {
        open_examples_page();
    }
    ESP_LOGI(TAG, "Question examples opened by voice");
}

void ui_settings_close(void)
{
    close_text_entry();

    /*
     * Sub-pages are siblings of the main page rather than children of it, so
     * each one has to be taken down by hand. Miss one and it stays on screen
     * with nothing behind it.
     */
    if (s_examples_page != NULL) {
        lv_obj_del(s_examples_page);
        s_examples_page = NULL;
    }
    if (s_wifi_status_timer != NULL) {
        lv_timer_del(s_wifi_status_timer);
        s_wifi_status_timer = NULL;
    }
    if (s_wifi_page != NULL) {
        lv_obj_del(s_wifi_page);
        s_wifi_page = NULL;
        s_wifi_network_list = NULL;
        s_wifi_status_label = NULL;
    }
    if (s_main_page_timer != NULL) {
        lv_timer_del(s_main_page_timer);
        s_main_page_timer = NULL;
    }
    /*
     * The expressions page, which used to be left behind entirely.
     *
     * Every page here is a full-screen, opaque, clickable sibling on the active
     * screen, so deleting the main page does NOT take a page sitting on top of
     * it. And the handler that calls this function is a row INSIDE the
     * expressions page: tapping a face to preview it closed settings, left the
     * expressions list on screen swallowing every touch, and played the
     * four-second preview completely hidden behind it. The gear button
     * underneath was covered too, so the only way out was the page's own back
     * arrow — which looked like the buddy had frozen.
     */
    if (s_expressions_page != NULL) {
        lv_obj_del(s_expressions_page);
        s_expressions_page = NULL;
    }
    if (s_main_page != NULL) {
        lv_obj_del(s_main_page);
        s_main_page = NULL;
        /*
         * These point INTO the page that was just deleted. Leaving them set
         * would give the refresh timer — or anything else that ran before the
         * pointers were reused — a handle to freed memory.
         */
        s_wifi_row_value = NULL;
        s_api_key_row_value = NULL;
        s_home_url_row_value = NULL;
        s_requests_row_value = NULL;
        s_about_label = NULL;
    }

    ESP_LOGI(TAG, "Settings closed");
}

bool ui_settings_is_open(void)
{
    /*
     * Every page, not just the ones that happen to be listed first. The rest of
     * the firmware uses this to decide whether the face is visible — a stale
     * "no" while the expressions list is up means the buddy pulls faces at a
     * screen nobody can see.
     */
    return (s_main_page != NULL) ||
           (s_wifi_page != NULL) ||
           (s_text_entry_page != NULL) ||
           (s_examples_page != NULL) ||
           (s_expressions_page != NULL);
}
