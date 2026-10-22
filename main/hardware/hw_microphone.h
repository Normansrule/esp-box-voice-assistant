/*
 * hw_microphone.h — Everything that goes INTO the box.
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 *
 * The ESP32-S3-BOX-3 has a two-microphone array. Samples arrive interleaved as
 * stereo, but the speech front end wants three channels: microphone one,
 * microphone two, and a reference channel for echo cancellation. This module
 * reads the codec and rearranges the samples into that layout, so the voice
 * code never has to think about frame geometry.
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Prepare the capture path.
 *
 * @note Call after bsp_board_init(); the codec itself is brought up there.
 */
esp_err_t hw_microphone_init(void);

/**
 * @brief Read one chunk of audio, laid out the way the speech engine wants it.
 *
 * Blocks until a full chunk is available.
 *
 * @param destination     Buffer of at least (samples_per_channel * 3) int16 values.
 * @param samples_per_channel  Frame length the speech engine asked for.
 * @return ESP_OK on success.
 */
esp_err_t hw_microphone_read_frame(int16_t *destination, size_t samples_per_channel);

/**
 * @brief Stop or resume feeding the speech engine.
 *
 * The hardware mute switch on the top of the box calls this. While muted,
 * hw_microphone_read_frame() still returns, but filled with silence, so the
 * wake word simply never triggers.
 */
void hw_microphone_set_muted(bool muted);

/**
 * @brief True when the microphone is currently muted.
 */
bool hw_microphone_is_muted(void);

#ifdef __cplusplus
}
#endif
