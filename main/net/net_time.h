/*
 * net_time.h — Getting the clock right.
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 *
 * The ESP32-S3 has no battery-backed clock, so every power-up starts in 1970
 * until a time server is reached. This module asks one as soon as Wi-Fi comes
 * up and applies the user's time zone, which is what lets the buddy answer
 * "what time is it" with a local, human-sounding answer.
 */

#pragma once

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Start asking network time servers for the time.
 *
 * Safe to call before Wi-Fi is up; the request retries in the background.
 */
esp_err_t net_time_start(void);

/**
 * @brief Apply a time zone.
 *
 * @param posix_timezone  A POSIX TZ string such as "PST8PDT,M3.2.0,M11.1.0".
 *                        Pass NULL to use the one in the settings.
 */
void net_time_set_timezone(const char *posix_timezone);

/**
 * @brief True once a time server has answered and the clock is believable.
 */
bool net_time_is_synchronised(void);

#ifdef __cplusplus
}
#endif
