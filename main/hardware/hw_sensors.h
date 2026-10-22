/*
 * hw_sensors.h — Temperature, humidity and presence from the sensor dock.
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 *
 * The ESP32-S3-BOX-3 sensor dock that clips onto the bottom of the box carries
 * a temperature/humidity sensor and a millimetre-wave presence radar. Both are
 * polled by the board support package once a second; this module wraps that,
 * converts units, and tells you politely when the dock is not attached.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A single reading from the environment sensor.
 */
typedef struct {
    bool  valid;                /*!< false when the dock is missing or unread */
    float temperature_celsius;
    float humidity_percent;
} hw_sensor_reading_t;

/**
 * @brief Check whether the sensor dock is attached and start using it.
 *
 * @note Call after bsp_board_init().
 * @return ESP_OK when the dock is present, ESP_ERR_NOT_FOUND when it is not.
 *         A missing dock is not fatal — the readout simply hides itself.
 */
esp_err_t hw_sensors_init(void);

/**
 * @brief True when the sensor dock was detected at start-up.
 */
bool hw_sensors_is_dock_present(void);

/**
 * @brief Take the latest environment reading.
 *
 * @return A reading whose `valid` flag says whether the numbers mean anything.
 */
hw_sensor_reading_t hw_sensors_read_environment(void);

/**
 * @brief True when the radar currently sees somebody near the desk.
 *
 * Returns false when no dock is fitted.
 */
bool hw_sensors_is_someone_nearby(void);

/**
 * @brief Convert a Celsius reading to Fahrenheit.
 */
float hw_sensors_celsius_to_fahrenheit(float celsius);

#ifdef __cplusplus
}
#endif
