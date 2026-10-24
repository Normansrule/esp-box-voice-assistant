/*
 * hw_imu.h — The motion sensor inside the BOX-3.
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 *
 * The BOX-3 main board carries a 6-axis IMU on the shared I2C bus. Espressif's
 * BSP documents it as an ICM-42607-P and drives it with the espressif/icm42670
 * component, which accepts either chip by WHO_AM_I (0x60 = ICM-42607-P,
 * 0x67 = ICM-42670-P). This file reads whichever answers and reports which.
 *
 * STATUS: UNTESTED ON DEVICE. Axis directions on the real board, the shake
 * threshold and the tilt gain all need a log from a real box before they are
 * trusted. See docs/HARDWARE_TESTS.md for the commands that collect one.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** One reading. Acceleration in g, rotation in degrees per second. */
typedef struct {
    bool  valid;          /*!< false until the first successful read        */
    float accel_g[3];     /*!< x, y, z as the chip reports them              */
    float gyro_dps[3];    /*!< x, y, z as the chip reports them              */
    float tilt_x_deg;     /*!< left/right lean relative to the resting pose  */
    float tilt_y_deg;     /*!< forward/back lean relative to the resting pose*/
    float temp_c;         /*!< the IMU die temperature, not the room         */
} hw_imu_sample_t;

typedef enum {
    HW_IMU_GESTURE_SHAKE = 0,   /*!< several hard jolts in quick succession */
    HW_IMU_GESTURE_PICKED_UP,   /*!< lifted off the desk: a sustained lean  */
    HW_IMU_GESTURE_SET_DOWN,    /*!< back at the resting pose               */
} hw_imu_gesture_t;

typedef void (*hw_imu_gesture_cb_t)(hw_imu_gesture_t gesture);

/**
 * @brief Find the IMU, configure it and start the sampling task.
 *
 * Needs bsp_i2c_init() first. Returns ESP_ERR_NOT_FOUND when nothing answers
 * at 0x68, which is not fatal: everything else works without motion.
 */
esp_err_t hw_imu_init(void);

/** True once a chip answered with a recognised WHO_AM_I. */
bool hw_imu_is_present(void);

/** The WHO_AM_I byte read at start-up (0 when absent). */
uint8_t hw_imu_chip_id(void);

/** "ICM-42607-P", "ICM-42670-P", "unknown" or "absent". */
const char *hw_imu_chip_name(void);

/** The most recent sample (copied; safe from any task). */
hw_imu_sample_t hw_imu_latest(void);

/** Called from the IMU task when a gesture is recognised. Keep it short. */
void hw_imu_set_gesture_callback(hw_imu_gesture_cb_t callback);

/** Treat the current orientation as "level" for the tilt numbers. */
void hw_imu_zero(void);

/**
 * @brief Print one CSV line per sample on the console while on.
 *
 * Format, for spreadsheets and docs/HARDWARE_TESTS.md:
 *   IMU,<ms>,<ax>,<ay>,<az>,<gx>,<gy>,<gz>,<tilt_x>,<tilt_y>,<temp_c>
 */
void hw_imu_set_stream(bool on, uint16_t every_nth_sample);

#ifdef __cplusplus
}
#endif
