/*
 * hw_battery.c — Measuring the dock's 18650 cell through IO10.
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 *
 * STATUS: UNTESTED ON DEVICE. See hw_battery.h for the circuit and limits.
 */

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"

#include "buddy_config.h"
#include "hw_battery.h"
#include "hw_sensors.h"

static const char *TAG = "hw_battery";

/* IO10 is ADC1 channel 9 on the ESP32-S3. */
#define BATTERY_ADC_UNIT        ADC_UNIT_1
#define BATTERY_ADC_CHANNEL     ADC_CHANNEL_9

/*
 * The pin sees at most 4.2 V / 4.01 = 1.05 V, so the 2.5 dB range
 * (about 0 to 1.25 V on the S3) keeps the most resolution.
 */
#define BATTERY_ADC_ATTEN       ADC_ATTEN_DB_2_5

/* R15 = 301k over R16 = 100k: battery = pin x 401 / 100. */
#define DIVIDER_NUMERATOR       401
#define DIVIDER_DENOMINATOR     100

#define SAMPLES_PER_READING     64

static adc_oneshot_unit_handle_t s_adc;
static adc_cali_handle_t         s_cali;
static SemaphoreHandle_t         s_lock;

/*
 * Resting open-circuit voltage against remaining charge for a typical
 * Li-ion 18650 cell. Real cells differ by a few percent; this is why the
 * number on screen carries a "~".
 */
static const struct { uint16_t mv; uint8_t percent; } s_curve[] = {
    { 4200, 100 }, { 4100, 90 }, { 4000, 80 }, { 3920, 70 }, { 3850, 60 },
    { 3790, 50 },  { 3750, 40 }, { 3710, 30 }, { 3670, 20 }, { 3610, 10 },
    { 3500, 5 },   { 3300, 0 },
};

uint8_t hw_battery_estimate_percent(uint16_t battery_mv)
{
    const size_t n = sizeof(s_curve) / sizeof(s_curve[0]);
    if (battery_mv >= s_curve[0].mv) {
        return 100;
    }
    if (battery_mv <= s_curve[n - 1].mv) {
        return 0;
    }
    for (size_t i = 1; i < n; i++) {
        if (battery_mv >= s_curve[i].mv) {
            const int span_mv = s_curve[i - 1].mv - s_curve[i].mv;
            const int span_pc = s_curve[i - 1].percent - s_curve[i].percent;
            return (uint8_t)(s_curve[i].percent + ((battery_mv - s_curve[i].mv) * span_pc) / span_mv);
        }
    }
    return 0;
}

esp_err_t hw_battery_init(void)
{
    if (s_adc != NULL) {
        return ESP_OK;
    }

    const adc_oneshot_unit_init_cfg_t unit_cfg = { .unit_id = BATTERY_ADC_UNIT };
    esp_err_t err = adc_oneshot_new_unit(&unit_cfg, &s_adc);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "ADC1 unavailable: %s", esp_err_to_name(err));
        return err;
    }

    const adc_oneshot_chan_cfg_t chan_cfg = {
        .atten = BATTERY_ADC_ATTEN,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    err = adc_oneshot_config_channel(s_adc, BATTERY_ADC_CHANNEL, &chan_cfg);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "IO10 channel setup failed: %s", esp_err_to_name(err));
        return err;
    }

    const adc_cali_curve_fitting_config_t cali_cfg = {
        .unit_id = BATTERY_ADC_UNIT,
        .chan = BATTERY_ADC_CHANNEL,
        .atten = BATTERY_ADC_ATTEN,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    if (adc_cali_create_scheme_curve_fitting(&cali_cfg, &s_cali) != ESP_OK) {
        s_cali = NULL;
        ESP_LOGW(TAG, "No eFuse ADC calibration; voltages will be less accurate");
    }

    s_lock = xSemaphoreCreateMutex();
    ESP_LOGI(TAG, "Battery sense on IO10 (ADC1 ch9), divider x%d.%02d%s",
             DIVIDER_NUMERATOR / DIVIDER_DENOMINATOR, DIVIDER_NUMERATOR % DIVIDER_DENOMINATOR,
             hw_sensors_is_dock_present() ? "" : " - no sensor dock, readings will be ignored");
    return ESP_OK;
}

hw_battery_reading_t hw_battery_read(void)
{
    hw_battery_reading_t reading = { 0 };

    if (s_adc == NULL || !hw_sensors_is_dock_present()) {
        return reading;
    }
    if (s_lock != NULL && xSemaphoreTake(s_lock, pdMS_TO_TICKS(50)) != pdTRUE) {
        return reading;
    }

    int32_t sum_mv = 0;
    int good = 0;
    for (int i = 0; i < SAMPLES_PER_READING; i++) {
        int raw = 0;
        if (adc_oneshot_read(s_adc, BATTERY_ADC_CHANNEL, &raw) != ESP_OK) {
            continue;
        }
        int mv = 0;
        if (s_cali != NULL) {
            adc_cali_raw_to_voltage(s_cali, raw, &mv);
        } else {
            mv = (raw * 1250) / 4095;   /* nominal full scale of the 2.5 dB range */
        }
        sum_mv += mv;
        good++;
    }
    if (s_lock != NULL) {
        xSemaphoreGive(s_lock);
    }
    if (good == 0) {
        return reading;
    }

    reading.available = true;
    reading.pin_mv = (uint16_t)(sum_mv / good);
    reading.battery_mv = (uint16_t)(((uint32_t)reading.pin_mv * DIVIDER_NUMERATOR) / DIVIDER_DENOMINATOR);
    reading.cell_detected = reading.battery_mv >= BUDDY_BATTERY_PRESENT_MV;
    if (reading.cell_detected) {
        reading.percent_est = hw_battery_estimate_percent(reading.battery_mv);
        reading.low = reading.battery_mv < BUDDY_BATTERY_LOW_MV;
    }
    return reading;
}
