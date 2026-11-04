/*
 * buddy_motion.c — Turning tilt, shakes and the battery into behaviour.
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 *
 * STATUS: UNTESTED ON DEVICE.
 *
 * Everything runs on one low-priority task so that neither the IMU task nor
 * the esp_timer service task ever waits on the LVGL lock: the IMU callback
 * only sets a flag, and this task does the drawing.
 */

#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "esp_timer.h"

#include "buddy_config.h"
#include "buddy_motion.h"
#include "face_emotions.h"
#include "hw_battery.h"
#include "hw_imu.h"
#include "ui_screen_face.h"
#include "ui_speech_bubble.h"

static const char *TAG = "motion";

static volatile uint32_t s_pending;          /* bit per hw_imu_gesture_t */
static volatile bool     s_gaze_enabled = true;

void buddy_motion_set_gaze_enabled(bool enabled)
{
    s_gaze_enabled = enabled;
    if (!enabled) {
        ui_screen_face_set_gaze_bias(0, 0);
    }
}

bool buddy_motion_get_gaze_enabled(void)
{
    return s_gaze_enabled;
}

static void on_gesture(hw_imu_gesture_t gesture)
{
    s_pending |= (1u << gesture);   /* handled on the motion task */
}

static int16_t clamp_look(float value)
{
    if (value > 100.0f) {
        return 100;
    }
    if (value < -100.0f) {
        return -100;
    }
    return (int16_t)value;
}

static void handle_gestures(void)
{
    const uint32_t pending = s_pending;
    s_pending = 0;

    if (pending & (1u << HW_IMU_GESTURE_SHAKE)) {
        ui_screen_face_flash_emotion(FACE_EMOTION_DIZZY, 1800);
        ui_screen_face_show_caption(UI_SPEECH_FROM_BUDDY, "Whoa! Everything is spinning.", 2200);
    } else if (pending & (1u << HW_IMU_GESTURE_PICKED_UP)) {
        ui_screen_face_flash_emotion(FACE_EMOTION_SURPRISED, 1200);
    } else if (pending & (1u << HW_IMU_GESTURE_SET_DOWN)) {
        ui_screen_face_flash_emotion(FACE_EMOTION_RELIEVED, 1400);
    }
}

static void check_battery(bool *warned)
{
    const hw_battery_reading_t b = hw_battery_read();
    if (!b.available) {
        return;
    }

    /* One line per reading, for docs/HARDWARE_TESTS.md and the Connect app. */
    printf("BATT,%lld,%u,%u,%u\n", (long long)(esp_timer_get_time() / 1000),
           b.pin_mv, b.battery_mv, b.percent_est);

    if (!b.cell_detected) {
        *warned = false;   /* nothing to warn about until a cell shows up */
    } else if (b.low && !*warned) {
        char text[64];
        snprintf(text, sizeof(text), "My battery is low: %u.%02u V.",
                 b.battery_mv / 1000, (b.battery_mv % 1000) / 10);
        ui_screen_face_flash_emotion(FACE_EMOTION_EXHAUSTED, 2500);
        ui_screen_face_show_caption(UI_SPEECH_STATUS, text, 5000);
        ESP_LOGW(TAG, "%s", text);
        *warned = true;
    } else if (b.battery_mv > BUDDY_BATTERY_LOW_MV + 150) {
        *warned = false;   /* charged back up: warn again next time */
    }
}

static void motion_task(void *argument)
{
    (void)argument;

    bool battery_warned = false;
    int64_t next_battery_us = 0;
    int16_t last_x = 0, last_y = 0;

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(BUDDY_IMU_GAZE_REFRESH_MS));

        if (hw_imu_is_present()) {
            handle_gestures();

            if (s_gaze_enabled) {
                const hw_imu_sample_t s = hw_imu_latest();
                if (s.valid) {
                    /* Eyes slide towards the low side, like a marble would. */
                    const int16_t x = clamp_look(s.tilt_x_deg * BUDDY_IMU_GAZE_PER_DEG);
                    const int16_t y = clamp_look(s.tilt_y_deg * BUDDY_IMU_GAZE_PER_DEG);
                    if (x != last_x || y != last_y) {
                        ui_screen_face_set_gaze_bias(x, y);
                        last_x = x;
                        last_y = y;
                    }
                }
            }
        }

        const int64_t now_us = esp_timer_get_time();
        if (now_us >= next_battery_us) {
            next_battery_us = now_us + (int64_t)BUDDY_BATTERY_REFRESH_MS * 1000;
            check_battery(&battery_warned);
        }
    }
}

esp_err_t buddy_motion_start(void)
{
    hw_imu_set_gesture_callback(on_gesture);

    if (xTaskCreate(motion_task, "motion", 3584, NULL, 2, NULL) != pdPASS) {
        ESP_LOGE(TAG, "Could not start the motion task");
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "Motion: %s; battery sense %s", hw_imu_chip_name(),
             hw_battery_read().available ? "on" : "unavailable");
    return ESP_OK;
}
