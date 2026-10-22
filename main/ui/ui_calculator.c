/*
 * ui_calculator.c — The keypad, so any sum can be done with nothing connected.
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */

#include <string.h>

#include "esp_log.h"

#include "audio_voice.h"
#include "buddy_calculator.h"
#include "buddy_config.h"
#include "face_theme.h"
#include "hw_display.h"
#include "ui_calculator.h"
#include "ui_screen_face.h"
#include "voice_number_words.h"
#include "voice_speech_parts.h"
#include "voice_wakeword.h"

static const char *TAG = "ui_calc";

/** Height of the strip at the top holding the back arrow and the title. */
#define HEADER_HEIGHT       32

/** Height of the number display under it. */
#define DISPLAY_HEIGHT      44

static lv_obj_t *s_page;
static lv_obj_t *s_display_label;
static lv_obj_t *s_hint_label;

static buddy_calculator_t s_calc;

/* ------------------------------------------------------------------------ */
/* Saying the answer                                                         */
/* ------------------------------------------------------------------------ */

/**
 * @brief Read the result out, if there is one worth reading.
 *
 * Only after equals — announcing every keypress would be unbearable, and
 * buddy_calculator_has_spoken_answer() is exactly that distinction.
 *
 * The hold-then-play dance is the same one voice_assistant.c uses: take
 * VOICE_HOLD_SPEAKING before starting, and let it go again if nothing started.
 * When playback does start, the speaker's finished callback releases it, so a
 * calculator answer behaves like any other thing the buddy says — the
 * microphone stays shut until it has finished talking.
 */
static void speak_result(void)
{
    if (!buddy_calculator_has_spoken_answer(&s_calc)) {
        return;
    }

    voice_speech_plan_t plan;
    voice_speech_plan_reset(&plan);

    voice_speech_plan_add(&plan, VOICE_FRAGMENT_THE_ANSWER_IS);
    voice_speech_plan_add_decimal(&plan, buddy_calculator_value(&s_calc));

    if (!voice_speech_plan_is_usable(&plan)) {
        /*
         * Too large to say, which for this bank means a million or more. The
         * figure is on the display either way; going quiet is the honest
         * response, and the hint line below says so.
         */
        ESP_LOGI(TAG, "%s is past what I can say aloud",
                 buddy_calculator_display(&s_calc));
        if (s_hint_label != NULL) {
            lv_label_set_text(s_hint_label, "Too big to say out loud");
        }
        return;
    }

    if (s_hint_label != NULL) {
        lv_label_set_text(s_hint_label, "");
    }

    voice_wakeword_hold(VOICE_HOLD_SPEAKING);

    if (!audio_voice_speak_parts(plan.parts, plan.count)) {
        voice_wakeword_release(VOICE_HOLD_SPEAKING);
    }
}

/* ------------------------------------------------------------------------ */
/* The keypad                                                                */
/* ------------------------------------------------------------------------ */

/*
 * The button grid, in LVGL's button-matrix notation: "\n" ends a row and the
 * empty string ends the map. Laid out like every pocket calculator ever made,
 * because the whole appeal of a four-function calculator is that nobody has to
 * be taught where the keys are.
 */
/*
 * Deliberately `const char *` and not `const char *const`, which is what the
 * contents actually are. LVGL's lv_btnmatrix_set_map() takes `const char *[]`,
 * and the stricter type will not convert to it — C does not allow adding const
 * two levels down. Writing the array the way the function wants it is better
 * than casting the constness away at the call site, where the cast would be
 * one more thing to reason about every time someone reads it.
 */
static const char *s_keypad_map[] = {
    "C",   LV_SYMBOL_BACKSPACE, "%", "/", "\n",
    "7",   "8",                 "9", "x", "\n",
    "4",   "5",                 "6", "-", "\n",
    "1",   "2",                 "3", "+", "\n",
    "+/-", "0",                 ".", "=", "",
};

/**
 * @brief Turn a button's label into the key the engine understands.
 *
 * The labels and the keys differ in three places on purpose: the multiply key
 * reads "x" because that is what it looks like on a calculator and "*" is a
 * programmer's habit; backspace is an icon; and "+/-" is two characters wide.
 */
static char key_for_label(const char *label)
{
    if (label == NULL || label[0] == '\0') {
        return '\0';
    }
    if (strcmp(label, "x") == 0) {
        return BUDDY_CALC_KEY_MULTIPLY;
    }
    if (strcmp(label, "+/-") == 0) {
        return BUDDY_CALC_KEY_SIGN;
    }
    if (strcmp(label, LV_SYMBOL_BACKSPACE) == 0) {
        return BUDDY_CALC_KEY_BACKSPACE;
    }
    return label[0];
}

static void keypad_cb(lv_event_t *event)
{
    lv_obj_t *matrix = lv_event_get_target(event);
    const char *label = lv_btnmatrix_get_btn_text(matrix, lv_btnmatrix_get_selected_btn(matrix));

    const char key = key_for_label(label);
    if (key == '\0') {
        return;
    }

    buddy_calculator_press(&s_calc, key);

    if (s_display_label != NULL) {
        lv_label_set_text(s_display_label, buddy_calculator_display(&s_calc));
    }

    /* Clearing an old "too big to say" note as soon as anything else happens. */
    if (s_hint_label != NULL && key != BUDDY_CALC_KEY_EQUALS) {
        lv_label_set_text(s_hint_label, "");
    }

    if (key == BUDDY_CALC_KEY_EQUALS) {
        speak_result();
    }
}

static void back_cb(lv_event_t *event)
{
    (void)event;
    ui_calculator_close();
}

/* ------------------------------------------------------------------------ */
/* The page                                                                  */
/* ------------------------------------------------------------------------ */

esp_err_t ui_calculator_open(void)
{
    if (s_page != NULL) {
        return ESP_OK;
    }
    if (!hw_display_lock(500)) {
        return ESP_ERR_TIMEOUT;
    }

    buddy_calculator_reset(&s_calc);

    s_page = lv_obj_create(lv_scr_act());
    lv_obj_remove_style_all(s_page);
    lv_obj_set_size(s_page, BUDDY_SCREEN_WIDTH, BUDDY_SCREEN_HEIGHT);
    lv_obj_set_style_bg_color(s_page, FACE_COLOR_BACKGROUND, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_page, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_clear_flag(s_page, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_page, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_move_foreground(s_page);

    /* --- header ------------------------------------------------------- */
    lv_obj_t *back = lv_label_create(s_page);
    lv_label_set_text(back, LV_SYMBOL_LEFT " Back");
    lv_obj_set_style_text_color(back, FACE_COLOR_ESPRESSIF_RED, LV_PART_MAIN);
    lv_obj_align(back, LV_ALIGN_TOP_LEFT, 8, 8);
    lv_obj_add_flag(back, LV_OBJ_FLAG_CLICKABLE);
    /* A generous touch target — the label itself is only a few pixels tall. */
    lv_obj_set_ext_click_area(back, 14);
    lv_obj_add_event_cb(back, back_cb, LV_EVENT_CLICKED, NULL);

    /* --- display ------------------------------------------------------ */
    lv_obj_t *display_box = lv_obj_create(s_page);
    lv_obj_remove_style_all(display_box);
    lv_obj_set_size(display_box, BUDDY_SCREEN_WIDTH - 16, DISPLAY_HEIGHT);
    lv_obj_set_pos(display_box, 8, HEADER_HEIGHT);
    lv_obj_set_style_bg_color(display_box, FACE_COLOR_SURFACE, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(display_box, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(display_box, 8, LV_PART_MAIN);
    lv_obj_clear_flag(display_box, LV_OBJ_FLAG_SCROLLABLE);

    s_display_label = lv_label_create(display_box);
    lv_label_set_text(s_display_label, buddy_calculator_display(&s_calc));
    lv_obj_set_style_text_font(s_display_label, &lv_font_montserrat_16, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_display_label, FACE_COLOR_TEXT, LV_PART_MAIN);
    lv_label_set_long_mode(s_display_label, LV_LABEL_LONG_CLIP);
    lv_obj_align(s_display_label, LV_ALIGN_RIGHT_MID, -10, -4);

    s_hint_label = lv_label_create(display_box);
    lv_label_set_text(s_hint_label, "");
    lv_obj_set_style_text_font(s_hint_label, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_hint_label, FACE_COLOR_TEXT_MUTED, LV_PART_MAIN);
    lv_obj_align(s_hint_label, LV_ALIGN_BOTTOM_RIGHT, -10, -2);

    /* --- keypad ------------------------------------------------------- */
    lv_obj_t *keypad = lv_btnmatrix_create(s_page);
    lv_btnmatrix_set_map(keypad, s_keypad_map);
    lv_obj_set_size(keypad, BUDDY_SCREEN_WIDTH - 16,
                    BUDDY_SCREEN_HEIGHT - HEADER_HEIGHT - DISPLAY_HEIGHT - 12);
    lv_obj_set_pos(keypad, 8, HEADER_HEIGHT + DISPLAY_HEIGHT + 4);

    lv_obj_set_style_bg_opa(keypad, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(keypad, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(keypad, 0, LV_PART_MAIN);

    lv_obj_set_style_bg_color(keypad, FACE_COLOR_SURFACE, LV_PART_ITEMS);
    lv_obj_set_style_bg_opa(keypad, LV_OPA_COVER, LV_PART_ITEMS);
    lv_obj_set_style_text_color(keypad, FACE_COLOR_TEXT, LV_PART_ITEMS);
    lv_obj_set_style_border_color(keypad, FACE_COLOR_SURFACE_BORDER, LV_PART_ITEMS);
    lv_obj_set_style_border_width(keypad, 1, LV_PART_ITEMS);
    lv_obj_set_style_radius(keypad, 6, LV_PART_ITEMS);
    lv_obj_set_style_pad_all(keypad, 2, LV_PART_ITEMS);

    lv_obj_add_event_cb(keypad, keypad_cb, LV_EVENT_VALUE_CHANGED, NULL);

    hw_display_unlock();

    ESP_LOGI(TAG, "Calculator open");
    return ESP_OK;
}

void ui_calculator_close(void)
{
    if (s_page == NULL) {
        return;
    }
    if (!hw_display_lock(500)) {
        return;
    }

    lv_obj_del(s_page);
    s_page = NULL;
    s_display_label = NULL;
    s_hint_label = NULL;

    hw_display_unlock();

    ESP_LOGI(TAG, "Calculator closed");
}

bool ui_calculator_is_open(void)
{
    return s_page != NULL;
}
