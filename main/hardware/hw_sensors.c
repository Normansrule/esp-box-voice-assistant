/*
 * hw_sensors.c — Reading the sensor dock.
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */

#include "esp_log.h"

#include "bsp_board.h"

#include "hw_sensors.h"

static const char *TAG = "hw_sensors";

static bool s_dock_present;

esp_err_t hw_sensors_init(void)
{
    bsp_bottom_property_t *dock = bsp_board_get_sensor_handle();

    if (dock == NULL || dock->get_bottom_id == NULL) {
        ESP_LOGW(TAG, "No sensor interface on this board build");
        s_dock_present = false;
        return ESP_ERR_NOT_FOUND;
    }

    s_dock_present = (dock->get_bottom_id() == BOTTOM_ID_SENSOR);

    if (s_dock_present) {
        ESP_LOGI(TAG, "Sensor dock detected — temperature, humidity and radar available");
        /*
         * The radar is what lets the box wake itself when somebody walks up.
         * Enable it explicitly so the behaviour does not depend on whatever the
         * board support package happened to leave it set to.
         */
        if (dock->set_radar_enable != NULL) {
            dock->set_radar_enable(true);
        }
        return ESP_OK;
    }

    ESP_LOGW(TAG, "No sensor dock attached — the readout will stay hidden");
    return ESP_ERR_NOT_FOUND;
}

bool hw_sensors_is_dock_present(void)
{
    return s_dock_present;
}

hw_sensor_reading_t hw_sensors_read_environment(void)
{
    hw_sensor_reading_t reading = {
        .valid = false,
        .temperature_celsius = 0.0f,
        .humidity_percent = 0.0f,
    };

    if (!s_dock_present) {
        return reading;
    }

    bsp_bottom_property_t *dock = bsp_board_get_sensor_handle();
    if (dock == NULL || dock->get_humiture == NULL) {
        return reading;
    }

    float temperature = 0.0f;
    float humidity = 0.0f;
    if (dock->get_humiture(&temperature, &humidity) != ESP_OK) {
        return reading;
    }

    /*
     * A brand new dock reports zeros until its first conversion completes, and
     * an unplugged one reports nonsense. Sanity-check before believing it.
     */
    if (temperature < -40.0f || temperature > 90.0f ||
        humidity < 0.0f || humidity > 100.0f) {
        return reading;
    }

    reading.valid = true;
    reading.temperature_celsius = temperature;
    reading.humidity_percent = humidity;
    return reading;
}

bool hw_sensors_is_someone_nearby(void)
{
    if (!s_dock_present) {
        return false;
    }

    bsp_bottom_property_t *dock = bsp_board_get_sensor_handle();
    if (dock == NULL || dock->get_radar_status == NULL) {
        return false;
    }
    return dock->get_radar_status();
}

float hw_sensors_celsius_to_fahrenheit(float celsius)
{
    return (celsius * 9.0f / 5.0f) + 32.0f;
}
