/*
 * hw_microphone.c — Reading the microphone array.
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */

#include <string.h>

#include "freertos/FreeRTOS.h"

#include "esp_log.h"

#include "bsp/esp-bsp.h"
#include "bsp_board.h"

#include "buddy_config.h"
#include "hw_microphone.h"

static const char *TAG = "hw_mic";

/** The codec delivers two interleaved microphone channels. */
#define CAPTURE_CHANNEL_COUNT       2

/** The speech front end expects three: mic, mic, echo reference. */
#define ENGINE_CHANNEL_COUNT        3

static bool s_muted;

esp_err_t hw_microphone_init(void)
{
    /*
     * The codec is already configured by bsp_board_init(); all this does is
     * make sure the format matches what the speech models expect, in case a
     * previous playback left it at some other rate.
     */
    esp_err_t err = bsp_codec_set_fs(BUDDY_AUDIO_SAMPLE_RATE_HZ,
                                     BUDDY_AUDIO_BITS_PER_SAMPLE,
                                     I2S_SLOT_MODE_STEREO);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Could not set the capture format: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "Microphone ready (%d Hz, %d channels in)",
             BUDDY_AUDIO_SAMPLE_RATE_HZ, CAPTURE_CHANNEL_COUNT);
    return ESP_OK;
}

esp_err_t hw_microphone_read_frame(int16_t *destination, size_t samples_per_channel)
{
    if (destination == NULL || samples_per_channel == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    if (s_muted) {
        /*
         * Hand back silence rather than refusing to read. The speech engine
         * keeps ticking over happily on a stream of zeros, which means muting
         * and unmuting needs no special handling anywhere else.
         */
        memset(destination, 0, samples_per_channel * ENGINE_CHANNEL_COUNT * sizeof(int16_t));
        return ESP_OK;
    }

    const size_t stereo_bytes = samples_per_channel * CAPTURE_CHANNEL_COUNT * sizeof(int16_t);
    size_t bytes_read = 0;

    esp_err_t err = bsp_i2s_read(destination, stereo_bytes, &bytes_read, portMAX_DELAY);
    if (err != ESP_OK) {
        return err;
    }

    /*
     * Convert two interleaved channels into three, in place and backwards so
     * nothing is overwritten before it has been moved:
     *
     *   before:  [L0 R0][L1 R1][L2 R2] ...
     *   after:   [L0 R0 0][L1 R1 0][L2 R2 0] ...
     *
     * The third slot is the echo-cancellation reference. Acoustic echo
     * cancellation is switched off for this build (the buddy pauses its own
     * audio while it listens), so the reference is simply zeroed.
     */
    for (int sample = (int)samples_per_channel - 1; sample >= 0; sample--) {
        destination[sample * 3 + 2] = 0;
        destination[sample * 3 + 1] = destination[sample * 2 + 1];
        destination[sample * 3 + 0] = destination[sample * 2 + 0];
    }

    return ESP_OK;
}

void hw_microphone_set_muted(bool muted)
{
    if (s_muted != muted) {
        ESP_LOGI(TAG, "Microphone %s", muted ? "muted" : "live");
    }
    s_muted = muted;
}

bool hw_microphone_is_muted(void)
{
    return s_muted;
}
