/*
 * esp_log.h — A stand-in for ESP-IDF's logging macros, for host builds only.
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 *
 * Every module in this firmware logs, including ones that are otherwise pure
 * logic and well worth testing on a laptop. Rather than strip the logging out
 * to make them testable — which would make the firmware harder to debug in
 * exchange for making the tests easier to write — the test build swallows it.
 *
 * The arguments are still evaluated for their types, so a printf format that
 * does not match its arguments is still caught by -Wformat here rather than
 * only on the chip.
 *
 * This file is NEVER part of the firmware build.
 */

#pragma once

#include <stdio.h>

/*
 * Consume the arguments without printing.
 *
 * The tag is cast to void rather than ignored, so that a module's static TAG
 * still counts as used — otherwise every file under test trips
 * -Werror=unused-variable on the host but compiles cleanly on the chip, which
 * is a maddening way to spend an afternoon.
 */
#define ESP_LOG_SWALLOW(tag, ...)                       \
    do {                                                \
        (void)(tag);                                    \
        if (0) { printf(__VA_ARGS__); }                 \
    } while (0)

#define ESP_LOGE(tag, ...)      ESP_LOG_SWALLOW(tag, __VA_ARGS__)
#define ESP_LOGW(tag, ...)      ESP_LOG_SWALLOW(tag, __VA_ARGS__)
#define ESP_LOGI(tag, ...)      ESP_LOG_SWALLOW(tag, __VA_ARGS__)
#define ESP_LOGD(tag, ...)      ESP_LOG_SWALLOW(tag, __VA_ARGS__)
#define ESP_LOGV(tag, ...)      ESP_LOG_SWALLOW(tag, __VA_ARGS__)

static inline const char *esp_err_to_name(int code)
{
    (void)code;
    return "ESP_ERR (host stub)";
}
