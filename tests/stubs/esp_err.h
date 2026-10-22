/*
 * esp_err.h — A stand-in for ESP-IDF's error type, for host builds only.
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 *
 * The modules under test are pure logic, but a couple of their headers include
 * "esp_err.h" for the return type. Rather than pull in the whole framework just
 * to compile a table of numbers, the test build puts this tiny stand-in on the
 * include path ahead of everything else.
 *
 * This file is NEVER part of the firmware build. If a test ever needs more than
 * this, that is a sign the code being tested is not as pure as it should be —
 * which is useful information in itself.
 */

#pragma once

#include <stdint.h>

typedef int esp_err_t;

#define ESP_OK                  0
#define ESP_FAIL                -1
#define ESP_ERR_NO_MEM          0x101
#define ESP_ERR_INVALID_ARG     0x102
#define ESP_ERR_INVALID_STATE   0x103
#define ESP_ERR_INVALID_SIZE    0x104
#define ESP_ERR_NOT_FOUND       0x105
#define ESP_ERR_NOT_SUPPORTED   0x106
#define ESP_ERR_TIMEOUT         0x107
