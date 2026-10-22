/*
 * ui_sensor_readout.c — Drawing the environment chip.
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */

#include <stdio.h>

#include "esp_log.h"

#include "buddy_config.h"
#include "buddy_settings.h"
#include "face_theme.h"
#include "hw_sensors.h"
#include "ui_sensor_readout.h"

static const char *TAG = "ui_readout";

static lv_obj_t   *s_chip;
static lv_obj_t   *s_temperature_label;
static lv_obj_t   *s_humidity_label;
static lv_timer_t *s_refresh_timer;

/**
 * @brief Timer callback: pull a fresh reading and redraw the text.
 */
static void refresh_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    ui_sensor_readout_refresh();
}

void ui_sensor_readout_refresh(void)
{
    if (s_chip == NULL) {
        return;
    }

    const hw_sensor_reading_t reading = hw_sensors_read_environment();

    if (!reading.valid) {
        /*
         * No dock, or the first conversion has not finished. Hide rather than
         * show placeholder dashes — an empty corner looks deliberate.
         */
        lv_obj_add_flag(s_chip, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    if (buddy_settings_get()->show_sensor_readout) {
        lv_obj_clear_flag(s_chip, LV_OBJ_FLAG_HIDDEN);
    }

    const buddy_settings_t *settings = buddy_settings_get();

    /*
     * Formatted with the C library's snprintf rather than LVGL's own
     * lv_label_set_text_fmt. LVGL ships a cut-down printf that does not handle
     * %f at all unless it is specially configured, and silently printing
     * nonsense where the temperature should be is a miserable bug to chase.
     */
    char text[16];

    if (settings->temperature_unit == BUDDY_TEMPERATURE_FAHRENHEIT) {
        snprintf(text, sizeof(text), "%d°F",
                 (int)(hw_sensors_celsius_to_fahrenheit(reading.temperature_celsius) + 0.5f));
    } else {
        snprintf(text, sizeof(text), "%d°C", (int)(reading.temperature_celsius + 0.5f));
    }
    lv_label_set_text(s_temperature_label, text);

    snprintf(text, sizeof(text), "%d%%", (int)(reading.humidity_percent + 0.5f));
    lv_label_set_text(s_humidity_label, text);
}

esp_err_t ui_sensor_readout_create(lv_obj_t *parent)
{
    if (parent == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_chip != NULL) {
        return ESP_OK;
    }

    /* The pill itself: a pale rounded rectangle that does not fight the face. */
    s_chip = lv_obj_create(parent);
    lv_obj_remove_style_all(s_chip);
    lv_obj_clear_flag(s_chip, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(s_chip, LV_OBJ_FLAG_CLICKABLE);  /* Taps pass through to the face. */
    lv_obj_set_size(s_chip, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(s_chip, FACE_COLOR_SURFACE, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_chip, LV_OPA_COVER, LV_PART_MAIN);
    /*
     * Deliberately compact. The pill shares the top-right corner with the
     * buddy's right eyebrow, and every pixel it gives back is a pixel the face
     * can use — see FACE_SENSOR_CHIP_BOTTOM_Y in buddy_config.h.
     */
    lv_obj_set_style_radius(s_chip, 12, LV_PART_MAIN);
    lv_obj_set_style_pad_hor(s_chip, 9, LV_PART_MAIN);
    lv_obj_set_style_pad_ver(s_chip, 4, LV_PART_MAIN);
    lv_obj_align(s_chip, LV_ALIGN_TOP_RIGHT,
                 -BUDDY_SCREEN_EDGE_MARGIN, BUDDY_SCREEN_EDGE_MARGIN);

    /* Lay the two numbers out in a row with a gap between them. */
    lv_obj_set_flex_flow(s_chip, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(s_chip, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(s_chip, 8, LV_PART_MAIN);

    s_temperature_label = lv_label_create(s_chip);
    lv_obj_set_style_text_color(s_temperature_label, FACE_COLOR_ESPRESSIF_RED, LV_PART_MAIN);
    lv_obj_set_style_text_font(s_temperature_label, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_label_set_text(s_temperature_label, "--°");

    s_humidity_label = lv_label_create(s_chip);
    lv_obj_set_style_text_color(s_humidity_label, FACE_COLOR_TEXT_MUTED, LV_PART_MAIN);
    lv_obj_set_style_text_font(s_humidity_label, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_label_set_text(s_humidity_label, "--%");

    /* Hidden until the first believable reading arrives. */
    lv_obj_add_flag(s_chip, LV_OBJ_FLAG_HIDDEN);

    s_refresh_timer = lv_timer_create(refresh_timer_cb, BUDDY_SENSOR_REFRESH_MS, NULL);
    ui_sensor_readout_refresh();

    ESP_LOGI(TAG, "Sensor readout created");
    return ESP_OK;
}

void ui_sensor_readout_set_visible(bool visible)
{
    if (s_chip == NULL) {
        return;
    }
    if (visible) {
        ui_sensor_readout_refresh();   /* Reveals it only if there is data. */
    } else {
        lv_obj_add_flag(s_chip, LV_OBJ_FLAG_HIDDEN);
    }
}
