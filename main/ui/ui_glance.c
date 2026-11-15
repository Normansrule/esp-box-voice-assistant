/*
 * ui_glance.c — Drawing the glance page. See the header.
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 *
 * Built when opened and deleted when closed, like the settings pages, so it
 * costs nothing while the face is showing. Every widget is a plain label or
 * rectangle: no images, no shadows, no layer buffers.
 */

#include <time.h>

#include "esp_log.h"
#include "esp_timer.h"

#include "buddy_config.h"
#include "buddy_settings.h"
#include "face_theme.h"
#include "hw_battery.h"
#include "hw_sensors.h"
#include "lvgl.h"
#include "net_time.h"
#include "ui_glance.h"
#include "ui_glance_logic.h"
#include "ui_status_logic.h"
#include "voice_wakeword.h"

LV_FONT_DECLARE(font_glance_clock);
LV_FONT_DECLARE(font_glance_value);

static const char *TAG = "ui_glance";

/* Captions at 12 px when that size is compiled in (it is in sdkconfig.defaults). */
#if defined(LV_FONT_MONTSERRAT_12) && LV_FONT_MONTSERRAT_12
#define CAPTION_FONT        (&lv_font_montserrat_12)
#else
#define CAPTION_FONT        (&lv_font_montserrat_14)
#endif

#define TICK_MS             500
#define READINGS_EVERY_MS   5000
#define TILE_COUNT          3
#define TILE_GAP            8
#define TILE_HEIGHT         78
#define SIDE_MARGIN         12

static lv_obj_t   *s_page;
static lv_obj_t   *s_date;
static lv_obj_t   *s_clock;
static lv_obj_t   *s_tile_value[TILE_COUNT];
static lv_obj_t   *s_tile_caption[TILE_COUNT];
static lv_timer_t *s_tick;
static int64_t     s_opened_us;
static int64_t     s_readings_us;
static int         s_shown_minute = -2;

static void refresh_clock(void)
{
    const bool synced = net_time_is_synchronised();
    if (!synced) {
        if (s_shown_minute != -1) {
            s_shown_minute = -1;
            lv_label_set_text(s_clock, "--:--");
            lv_label_set_text(s_date, "Clock not set yet (needs Wi-Fi)");
        }
        return;
    }

    const time_t now = time(NULL);
    struct tm local;
    localtime_r(&now, &local);
    const int stamp = local.tm_yday * 1440 + local.tm_hour * 60 + local.tm_min;
    if (stamp == s_shown_minute) {
        return;
    }
    s_shown_minute = stamp;

    char text[24];
    ui_status_format_clock(local.tm_hour, local.tm_min, text, sizeof(text));
    lv_label_set_text(s_clock, text);

    if (strftime(text, sizeof(text), "%A %d %B", &local) == 0) {
        text[0] = '\0';
    }
    lv_label_set_text(s_date, text);
}

static void show_tile(int index, const ui_glance_tile_t *tile)
{
    lv_label_set_text(s_tile_value[index], tile->value);
    lv_label_set_text(s_tile_caption[index], tile->caption);
}

static void refresh_readings(void)
{
    ui_glance_tile_t tile;

    const hw_sensor_reading_t env = hw_sensors_read_environment();
    const bool fahrenheit =
        (buddy_settings_get()->temperature_unit == BUDDY_TEMPERATURE_FAHRENHEIT);
    ui_glance_temperature(env.valid, env.temperature_celsius, fahrenheit, &tile);
    show_tile(0, &tile);

    ui_glance_humidity(env.valid, env.humidity_percent, &tile);
    show_tile(1, &tile);

    const hw_battery_reading_t battery = hw_battery_read();
    ui_glance_battery(battery.available, battery.cell_detected, battery.percent_est,
                      battery.low, &tile);
    show_tile(2, &tile);
    lv_obj_set_style_text_color(s_tile_caption[2],
                                (battery.available && battery.cell_detected && battery.low)
                                    ? FACE_COLOR_ACCENT : FACE_COLOR_TEXT_MUTED,
                                LV_PART_MAIN);
}

static void tick_cb(lv_timer_t *timer)
{
    (void)timer;
    if (s_page == NULL) {
        return;
    }

    /* The face comes back the moment the buddy is listening: a real event. */
    if (voice_wakeword_is_listening()) {
        ESP_LOGI(TAG, "Wake word: back to the face");
        ui_glance_close();
        return;
    }

    const int64_t now = esp_timer_get_time();
    if (now - s_opened_us >= (int64_t)UI_GLANCE_TIMEOUT_MS * 1000) {
        ESP_LOGI(TAG, "Glance timed out");
        ui_glance_close();
        return;
    }

    refresh_clock();
    if (now - s_readings_us >= (int64_t)READINGS_EVERY_MS * 1000) {
        s_readings_us = now;
        refresh_readings();
    }
}

static void page_event_cb(lv_event_t *event)
{
    const lv_event_code_t code = lv_event_get_code(event);
    if (code == LV_EVENT_SHORT_CLICKED || code == LV_EVENT_GESTURE) {
        ESP_LOGI(TAG, "Glance closed by touch");
        ui_glance_close();
    }
}

static lv_obj_t *make_label(lv_obj_t *parent, const lv_font_t *font, lv_color_t colour)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_style_text_font(label, font, LV_PART_MAIN);
    lv_obj_set_style_text_color(label, colour, LV_PART_MAIN);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_label_set_text(label, "");
    return label;
}

void ui_glance_open(void)
{
    if (s_page != NULL) {
        return;
    }

    s_page = lv_obj_create(lv_scr_act());
    lv_obj_remove_style_all(s_page);
    lv_obj_set_size(s_page, BUDDY_SCREEN_WIDTH, BUDDY_SCREEN_HEIGHT);
    lv_obj_set_pos(s_page, 0, 0);
    lv_obj_set_style_bg_color(s_page, FACE_COLOR_BACKGROUND, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_page, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_clear_flag(s_page, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_page, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_page, page_event_cb, LV_EVENT_SHORT_CLICKED, NULL);
    lv_obj_add_event_cb(s_page, page_event_cb, LV_EVENT_GESTURE, NULL);

    /* Date, then the time: the one thing a desk clock is for, as big as it fits. */
    s_date = make_label(s_page, &lv_font_montserrat_16, FACE_COLOR_TEXT_MUTED);
    lv_obj_set_width(s_date, BUDDY_SCREEN_WIDTH - 2 * SIDE_MARGIN);
    lv_label_set_long_mode(s_date, LV_LABEL_LONG_DOT);
    lv_obj_align(s_date, LV_ALIGN_TOP_MID, 0, 16);

    s_clock = make_label(s_page, &font_glance_clock, FACE_COLOR_TEXT);
    lv_obj_align(s_clock, LV_ALIGN_TOP_MID, 0, 50);

    /* Three tiles along the bottom, same surface and radius as the chips. */
    const lv_coord_t tile_w =
        (BUDDY_SCREEN_WIDTH - 2 * SIDE_MARGIN - (TILE_COUNT - 1) * TILE_GAP) / TILE_COUNT;
    for (int i = 0; i < TILE_COUNT; i++) {
        lv_obj_t *tile = lv_obj_create(s_page);
        lv_obj_remove_style_all(tile);
        lv_obj_clear_flag(tile, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_clear_flag(tile, LV_OBJ_FLAG_CLICKABLE);   /* Taps close the page. */
        lv_obj_set_size(tile, tile_w, TILE_HEIGHT);
        lv_obj_set_pos(tile, SIDE_MARGIN + i * (tile_w + TILE_GAP),
                       BUDDY_SCREEN_HEIGHT - SIDE_MARGIN - TILE_HEIGHT);
        lv_obj_set_style_bg_color(tile, FACE_COLOR_SURFACE, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_radius(tile, 14, LV_PART_MAIN);

        s_tile_value[i] = make_label(tile, &font_glance_value, FACE_COLOR_TEXT);
        lv_obj_align(s_tile_value[i], LV_ALIGN_TOP_MID, 0, 12);

        s_tile_caption[i] = make_label(tile, CAPTION_FONT, FACE_COLOR_TEXT_MUTED);
        lv_obj_set_width(s_tile_caption[i], tile_w - 8);
        lv_label_set_long_mode(s_tile_caption[i], LV_LABEL_LONG_DOT);
        lv_obj_align(s_tile_caption[i], LV_ALIGN_BOTTOM_MID, 0, -12);
    }

    s_opened_us = esp_timer_get_time();
    s_readings_us = s_opened_us;
    s_shown_minute = -2;
    refresh_clock();
    refresh_readings();

    s_tick = lv_timer_create(tick_cb, TICK_MS, NULL);
    ESP_LOGI(TAG, "Glance opened (clock %s)",
             net_time_is_synchronised() ? "synced" : "not set");
}

void ui_glance_close(void)
{
    if (s_page == NULL) {
        return;
    }
    if (s_tick != NULL) {
        lv_timer_del(s_tick);
        s_tick = NULL;
    }
    /* Async: this can run from the page's own event callback. */
    lv_obj_del_async(s_page);
    s_page = NULL;
}

bool ui_glance_is_open(void)
{
    return s_page != NULL;
}
