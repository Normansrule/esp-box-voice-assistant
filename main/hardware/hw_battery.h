/*
 * hw_battery.h — The 18650 cell in the BOX-3-SENSOR dock.
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 *
 * What the hardware gives us, from Espressif's schematic
 * SCH_ESP32-S3-BOX-3-SENSOR-01_V1.1 (esp-box repo, hardware/):
 *
 *   VBAT -- R15 301k --+-- R16 100k -- GND
 *                      |
 *                      +-- BAT_MEAS_ADC -> IO10 (ADC1 channel 9)
 *
 * so the cell voltage is the pin voltage times (301 + 100) / 100 = 4.01.
 * There is no filter capacitor fitted (C4/C5 are marked NC), so each reading
 * averages many samples. Charging is done by an AP5056 linear charger
 * (R10 = 2k, about 500 mA) behind a DW01-A + 8205A protection pair. Its
 * CHRG and STDBY outputs only drive the red and green LEDs: the ESP32 cannot
 * see whether it is charging.
 *
 * Therefore:
 *   - the voltage is MEASURED (to the ADC's calibrated accuracy);
 *   - the percentage is ESTIMATED from a resting Li-ion voltage curve, and
 *     reads high while charging and low under heavy speaker load;
 *   - with no cell fitted the pin follows the charger output, so a full
 *     reading on a dock with an empty holder means nothing.
 *
 * STATUS: UNTESTED ON DEVICE. Compare against a multimeter on the cell with
 * the commands in docs/HARDWARE_TESTS.md before trusting the numbers.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool     available;      /*!< false without the sensor dock or the ADC  */
    uint16_t pin_mv;         /*!< calibrated voltage at IO10                */
    uint16_t battery_mv;     /*!< pin_mv x 4.01                             */
    uint8_t  percent_est;    /*!< ESTIMATE from the resting voltage curve   */
    bool     low;            /*!< below BUDDY_BATTERY_LOW_MV                */
} hw_battery_reading_t;

/** Set up ADC1 channel 9 with calibration. Only meaningful with the SENSOR dock. */
esp_err_t hw_battery_init(void);

/** Take an averaged reading now (a few milliseconds). Safe from any task. */
hw_battery_reading_t hw_battery_read(void);

/** Voltage-to-percent estimate, exposed for the host tests. */
uint8_t hw_battery_estimate_percent(uint16_t battery_mv);

#ifdef __cplusplus
}
#endif
