/*
 * hw_imu.c — Reading the BOX-3's motion sensor and noticing gestures.
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 *
 * STATUS: UNTESTED ON DEVICE. See hw_imu.h.
 *
 * One small task samples the chip at BUDDY_IMU_RATE_HZ. Each sample is turned
 * into a lean relative to the pose the box was sitting in when it settled
 * (it lives on a tilted dock, so "level" is whatever it rests at), and fed to
 * two tiny detectors:
 *
 *   shake      BUDDY_IMU_SHAKE_PEAKS jolts above BUDDY_IMU_SHAKE_G inside
 *              BUDDY_IMU_SHAKE_WINDOW_MS, then a refractory pause so one
 *              shake is one event.
 *   picked up  a lean beyond BUDDY_IMU_PICKUP_DEG held for half a second;
 *              "set down" when it comes back within half that.
 *
 * The I2C bus is shared with the touch panel and both codecs. The legacy
 * driver serialises transactions, and at 50 Hz with a 12-byte read this task
 * takes well under 1% of the bus.
 */

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "esp_timer.h"

#include "bsp/esp-box-3.h"
#include "icm42670.h"

#include "buddy_config.h"
#include "hw_imu.h"

static const char *TAG = "hw_imu";

#define WHO_AM_I_ICM42607P   0x60
#define WHO_AM_I_ICM42670P   0x67
#define RAD_TO_DEG           57.29578f

static icm42670_handle_t     s_imu;
static uint8_t               s_chip_id;
static SemaphoreHandle_t     s_lock;
static hw_imu_sample_t       s_latest;
static hw_imu_gesture_cb_t   s_callback;
static volatile bool         s_zero_requested = true;   /* zero once settled */
static volatile bool         s_stream;
static volatile uint16_t     s_stream_every = 5;

/*
 * Scale factors for the ranges set in hw_imu_init() (ACCE_FS_4G, GYRO_FS_500DPS).
 *
 * We convert raw counts ourselves instead of calling icm42670_get_acce_value()
 * / icm42670_get_gyro_value(). In espressif/icm42670 1.0.0 those work out the
 * range by reading the wrong bits of ACCEL_CONFIG0 (bits 4:3 instead of 6:5,
 * and the gyro version reads the accel register). With our 4 g / 100 Hz setup
 * that picks the 8 g scale, so every reading came out doubled: about 1.96 g at
 * rest on the desk (seen on device 2026-11-03), which kept tripping the
 * shake detector. Keep these in step with the config in hw_imu_init().
 */
#define ACCE_COUNTS_PER_G      8192.0f   /* +/-4 g    */
#define GYRO_COUNTS_PER_DPS    65.5f     /* +/-500 dps */

static esp_err_t read_scaled(icm42670_value_t *accel, icm42670_value_t *gyro)
{
    icm42670_raw_value_t ra, rw;
    esp_err_t err = icm42670_get_acce_raw_value(s_imu, &ra);
    if (err == ESP_OK) {
        err = icm42670_get_gyro_raw_value(s_imu, &rw);
    }
    if (err != ESP_OK) {
        return err;
    }
    accel->x = ra.x / ACCE_COUNTS_PER_G;
    accel->y = ra.y / ACCE_COUNTS_PER_G;
    accel->z = ra.z / ACCE_COUNTS_PER_G;
    gyro->x  = rw.x / GYRO_COUNTS_PER_DPS;
    gyro->y  = rw.y / GYRO_COUNTS_PER_DPS;
    gyro->z  = rw.z / GYRO_COUNTS_PER_DPS;
    return ESP_OK;
}

/* Gravity direction at rest, as a unit vector. */
static float s_rest[3] = { 0.0f, 0.0f, 1.0f };

bool hw_imu_is_present(void)
{
    return s_imu != NULL;
}

uint8_t hw_imu_chip_id(void)
{
    return s_chip_id;
}

const char *hw_imu_chip_name(void)
{
    if (s_imu == NULL) {
        return "absent";
    }
    switch (s_chip_id) {
    case WHO_AM_I_ICM42607P: return "ICM-42607-P";
    case WHO_AM_I_ICM42670P: return "ICM-42670-P";
    default:                 return "unknown";
    }
}

hw_imu_sample_t hw_imu_latest(void)
{
    hw_imu_sample_t copy = { 0 };
    if (s_lock != NULL && xSemaphoreTake(s_lock, pdMS_TO_TICKS(20)) == pdTRUE) {
        copy = s_latest;
        xSemaphoreGive(s_lock);
    }
    return copy;
}

void hw_imu_set_gesture_callback(hw_imu_gesture_cb_t callback)
{
    s_callback = callback;
}

void hw_imu_zero(void)
{
    s_zero_requested = true;
}

void hw_imu_set_stream(bool on, uint16_t every_nth_sample)
{
    s_stream_every = (every_nth_sample == 0) ? 1 : every_nth_sample;
    s_stream = on;
}

static void emit(hw_imu_gesture_t gesture)
{
    static const char *names[] = { "shake", "picked_up", "set_down" };
    ESP_LOGI(TAG, "Gesture: %s", names[gesture]);
    if (s_callback != NULL) {
        s_callback(gesture);
    }
}

/**
 * @brief Lean of the current gravity vector away from the resting one.
 *
 * Small-angle decomposition: the component of the change along the chip's
 * x axis is "left/right", along y is "forward/back". Good enough for steering
 * eyes; not a navigation-grade attitude estimate.
 */
static void lean_from_rest(const float g[3], float *tilt_x, float *tilt_y)
{
    const float norm = sqrtf(g[0] * g[0] + g[1] * g[1] + g[2] * g[2]);
    if (norm < 0.2f) {          /* free fall or a bad read: keep the last lean */
        return;
    }
    const float u[3] = { g[0] / norm, g[1] / norm, g[2] / norm };
    *tilt_x = asinf(fmaxf(-1.0f, fminf(1.0f, u[0] - s_rest[0]))) * RAD_TO_DEG;
    *tilt_y = asinf(fmaxf(-1.0f, fminf(1.0f, u[1] - s_rest[1]))) * RAD_TO_DEG;
}

static void imu_task(void *argument)
{
    (void)argument;

    const TickType_t period = pdMS_TO_TICKS(1000 / BUDDY_IMU_RATE_HZ);
    TickType_t wake = xTaskGetTickCount();

    /* Shake detector state. */
    int64_t peaks_us[8] = { 0 };
    int     peak_count = 0;
    bool    above = false;
    int64_t refractory_until_us = 0;

    /* Pick-up detector state. */
    bool    lifted = false;
    int     lean_samples = 0;

    /* Settling: average the first half second for the resting pose. */
    float   settle_sum[3] = { 0 };
    int     settle_count = 0;

    uint32_t sample_index = 0;
    uint32_t read_errors = 0;

    for (;;) {
        vTaskDelayUntil(&wake, period);

        icm42670_value_t a, w;
        if (read_scaled(&a, &w) != ESP_OK) {
            if ((++read_errors % 50) == 1) {
                ESP_LOGW(TAG, "Read failed (%lu so far)", (unsigned long)read_errors);
            }
            continue;
        }
        float temp_c = 0.0f;
        if ((sample_index % BUDDY_IMU_RATE_HZ) == 0) {
            icm42670_get_temp_value(s_imu, &temp_c);
        } else {
            temp_c = s_latest.temp_c;
        }

        const float g[3] = { a.x, a.y, a.z };
        const int64_t now_us = esp_timer_get_time();

        /* (Re)learn the resting pose over half a second when asked. */
        if (s_zero_requested) {
            settle_sum[0] += g[0];
            settle_sum[1] += g[1];
            settle_sum[2] += g[2];
            if (++settle_count >= BUDDY_IMU_RATE_HZ / 2) {
                const float n = sqrtf(settle_sum[0] * settle_sum[0] + settle_sum[1] * settle_sum[1] +
                                      settle_sum[2] * settle_sum[2]);
                /*
                 * At rest the only acceleration is gravity, so the average
                 * magnitude must be close to 1 g. Anything else means the
                 * scale is wrong (the 2x driver bug above read 1.96 g) and
                 * every gesture threshold will misfire, so say so loudly.
                 */
                const float rest_g = n / (float)settle_count;
                if (rest_g < 0.85f || rest_g > 1.15f) {
                    ESP_LOGW(TAG, "At rest the sensor reads %.2f g, not about 1 g: the "
                                  "accelerometer scale is wrong, gestures will misfire",
                             rest_g);
                } else {
                    ESP_LOGI(TAG, "At rest: %.2f g (scale looks right)", rest_g);
                }
                if (n > 0.1f) {
                    s_rest[0] = settle_sum[0] / n;
                    s_rest[1] = settle_sum[1] / n;
                    s_rest[2] = settle_sum[2] / n;
                    ESP_LOGI(TAG, "Resting pose: gravity (%.2f, %.2f, %.2f)",
                             s_rest[0], s_rest[1], s_rest[2]);
                }
                memset(settle_sum, 0, sizeof(settle_sum));
                settle_count = 0;
                s_zero_requested = false;
            }
        }

        float tilt_x = 0.0f, tilt_y = 0.0f;
        lean_from_rest(g, &tilt_x, &tilt_y);

        if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(5)) == pdTRUE) {
            s_latest.valid = true;
            memcpy(s_latest.accel_g, g, sizeof(g));
            s_latest.gyro_dps[0] = w.x;
            s_latest.gyro_dps[1] = w.y;
            s_latest.gyro_dps[2] = w.z;
            s_latest.tilt_x_deg = tilt_x;
            s_latest.tilt_y_deg = tilt_y;
            s_latest.temp_c = temp_c;
            xSemaphoreGive(s_lock);
        }

        if (s_stream && (sample_index % s_stream_every) == 0) {
            printf("IMU,%lld,%.3f,%.3f,%.3f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f\n",
                   (long long)(now_us / 1000), g[0], g[1], g[2], w.x, w.y, w.z,
                   tilt_x, tilt_y, temp_c);
        }
        sample_index++;

        if (s_zero_requested) {
            continue;   /* no gestures while learning the rest pose */
        }

        /* --- Shake: count rising edges of |a| - 1 g above the threshold. --- */
        const float jolt = fabsf(sqrtf(g[0] * g[0] + g[1] * g[1] + g[2] * g[2]) - 1.0f);
        /*
         * Hysteresis: a jolt only counts once the reading has dropped back
         * below half the threshold. Without it, sensor noise sitting right at
         * the threshold makes a burst of "rising edges" out of one bump.
         */
        const bool now_above = above ? (jolt > BUDDY_IMU_SHAKE_G * 0.5f)
                                     : (jolt > BUDDY_IMU_SHAKE_G);
        if (now_above && !above && now_us > refractory_until_us) {
            if (peak_count < (int)(sizeof(peaks_us) / sizeof(peaks_us[0]))) {
                peaks_us[peak_count++] = now_us;
            }
            /* Forget peaks that fell out of the window. */
            int kept = 0;
            for (int i = 0; i < peak_count; i++) {
                if (now_us - peaks_us[i] <= (int64_t)BUDDY_IMU_SHAKE_WINDOW_MS * 1000) {
                    peaks_us[kept++] = peaks_us[i];
                }
            }
            peak_count = kept;
            if (peak_count >= BUDDY_IMU_SHAKE_PEAKS) {
                peak_count = 0;
                refractory_until_us = now_us + 2000 * 1000;
                emit(HW_IMU_GESTURE_SHAKE);
            }
        }
        above = now_above;

        /* --- Picked up / set down: a sustained lean, with hysteresis. --- */
        const float lean = sqrtf(tilt_x * tilt_x + tilt_y * tilt_y);
        if (!lifted) {
            lean_samples = (lean > BUDDY_IMU_PICKUP_DEG) ? lean_samples + 1 : 0;
            if (lean_samples >= BUDDY_IMU_RATE_HZ / 2) {
                lifted = true;
                lean_samples = 0;
                emit(HW_IMU_GESTURE_PICKED_UP);
            }
        } else {
            lean_samples = (lean < BUDDY_IMU_PICKUP_DEG / 2) ? lean_samples + 1 : 0;
            if (lean_samples >= BUDDY_IMU_RATE_HZ) {
                lifted = false;
                lean_samples = 0;
                emit(HW_IMU_GESTURE_SET_DOWN);
            }
        }
    }
}

esp_err_t hw_imu_init(void)
{
    if (s_imu != NULL) {
        return ESP_OK;
    }

    s_imu = icm42670_create(BSP_I2C_NUM, ICM42670_I2C_ADDRESS);
    if (s_imu == NULL) {
        /* icm42670_create() returns NULL when WHO_AM_I is neither 0x60 nor 0x67. */
        ESP_LOGW(TAG, "No IMU answered at 0x%02x", ICM42670_I2C_ADDRESS);
        return ESP_ERR_NOT_FOUND;
    }

    if (icm42670_get_deviceid(s_imu, &s_chip_id) != ESP_OK ||
        (s_chip_id != WHO_AM_I_ICM42607P && s_chip_id != WHO_AM_I_ICM42670P)) {
        ESP_LOGW(TAG, "No IMU answered at 0x%02x (WHO_AM_I 0x%02x)", ICM42670_I2C_ADDRESS, s_chip_id);
        icm42670_delete(s_imu);
        s_imu = NULL;
        return ESP_ERR_NOT_FOUND;
    }

    const icm42670_cfg_t config = {
        .acce_fs = ACCE_FS_4G,
        .acce_odr = ACCE_ODR_100HZ,
        .gyro_fs = GYRO_FS_500DPS,
        .gyro_odr = GYRO_ODR_100HZ,
    };
    esp_err_t err = icm42670_config(s_imu, &config);
    err |= icm42670_acce_set_pwr(s_imu, ACCE_PWR_LOWNOISE);
    err |= icm42670_gyro_set_pwr(s_imu, GYRO_PWR_LOWNOISE);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "IMU configuration failed");
        icm42670_delete(s_imu);
        s_imu = NULL;
        return ESP_FAIL;
    }

    s_lock = xSemaphoreCreateMutex();
    if (s_lock == NULL ||
        xTaskCreate(imu_task, "imu", 3584, NULL, BUDDY_IMU_TASK_PRIORITY, NULL) != pdPASS) {
        ESP_LOGE(TAG, "Could not start the IMU task");
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "%s (WHO_AM_I 0x%02x) at %d Hz", hw_imu_chip_name(), s_chip_id, BUDDY_IMU_RATE_HZ);
    return ESP_OK;
}
