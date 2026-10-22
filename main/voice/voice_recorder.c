/*
 * voice_recorder.c — The PSRAM ring of recently spoken audio.
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */

#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "buddy_config.h"
#include "voice_recorder.h"
#include "voice_speech_gate.h"

static const char *TAG = "recorder";

/*
 * The lock timeout has to outlast a request that is still legitimately running,
 * or the buffer would be pulled out from under a live upload. Asserted rather
 * than commented, because the two constants live in different sections of
 * buddy_config.h and nothing else would notice them crossing over.
 */
_Static_assert(BUDDY_RECORDER_LOCK_MAX_HOLD_S > BUDDY_CLOUD_REQUEST_DEADLINE_S,
               "the recording lock must outlast the cloud request deadline");

/** Size of the canonical RIFF/WAVE header written in front of the samples. */
#define WAV_HEADER_BYTES        44

/*
 * One allocation holds the header and the samples together, so the WAV file can
 * be handed out as a single contiguous block with no copying:
 *
 *   [ 44-byte WAV header ][ 16-bit mono samples ................. ]
 *   ^ s_buffer            ^ s_samples
 */
static uint8_t *s_buffer;
static int16_t *s_samples;
static size_t   s_capacity_samples;
static size_t   s_recorded_samples;
static bool     s_recording;

/**
 * How much of what was recorded was actually somebody talking.
 *
 * Measured as the samples go past, which costs one multiply per sample and
 * saves a paid transcription of every cough. See voice_speech_gate.h.
 */
static voice_speech_gate_t s_gate;

/**
 * True while something else is still reading the buffer.
 *
 * There is one recording buffer, and voice_recorder_get_wav() hands out a
 * pointer straight into it rather than a copy — which is what keeps a ten
 * second recording out of a second ten seconds of PSRAM. The cost is that
 * starting a new recording while the old one is being uploaded rewrites the
 * bytes underneath the upload.
 *
 * That used to be prevented by keeping the microphone shut for the whole
 * request. It is prevented by this flag instead, so the ears can come back
 * after ten seconds without the hazard coming back with them: a wake word in
 * that window is still heard and can still be answered from the offline
 * vocabulary — it just does not get recorded, so it cannot reach the cloud
 * until the previous request has let go.
 */
static volatile bool s_buffer_in_use;

/**
 * When the claim above was taken, so that a claim nobody released expires.
 *
 * Every path that finishes a request releases the buffer, and each of those
 * paths has been checked. The reason for a timestamp anyway is what a missed
 * release LOOKS like from outside: the wake word still fires, the face still
 * reacts, and every question afterwards measures zero seconds long, so the
 * buddy says "I did not hear anything" for the rest of the session. That is
 * indistinguishable from a broken microphone, and it is not a state worth
 * being correct about — better to assume the claim leaked, take the buffer
 * back, and say so in the log where it can be found.
 */
static volatile int64_t s_buffer_claimed_us;

esp_err_t voice_recorder_init(void)
{
    if (s_buffer != NULL) {
        return ESP_OK;
    }

    const size_t total_bytes = WAV_HEADER_BYTES + BUDDY_RECORDING_BUFFER_BYTES;

    s_buffer = heap_caps_calloc(1, total_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (s_buffer == NULL) {
        ESP_LOGE(TAG, "No PSRAM for a %u byte recording buffer", (unsigned)total_bytes);
        return ESP_ERR_NO_MEM;
    }

    s_samples = (int16_t *)(s_buffer + WAV_HEADER_BYTES);
    s_capacity_samples = BUDDY_RECORDING_BUFFER_BYTES / sizeof(int16_t);
    s_recorded_samples = 0;

    ESP_LOGI(TAG, "Recording buffer ready (%d seconds)", BUDDY_MAX_RECORDING_SECONDS);
    return ESP_OK;
}

void voice_recorder_start(void)
{
    /*
     * Reset FIRST, before any decision about whether to record.
     *
     * The old order returned early on a busy buffer and left the previous
     * recording's measurements in place, so the router then judged this wake
     * word using numbers from the last one. Whatever happens below, the
     * measurements now describe this attempt and nothing else.
     */
    s_recorded_samples = 0;
    voice_speech_gate_reset(&s_gate);

    if (s_buffer_in_use) {
        const int64_t held_us = esp_timer_get_time() - s_buffer_claimed_us;

        if (held_us > (int64_t)BUDDY_RECORDER_LOCK_MAX_HOLD_S * 1000000LL) {
            /*
             * Held past any plausible request. Something forgot to let go, and
             * waiting politely for it means never recording again.
             */
            ESP_LOGE(TAG, "Recording buffer was claimed %lld s ago and never released; "
                          "taking it back", held_us / 1000000LL);
            s_buffer_in_use = false;
        } else {
            /*
             * A live request really is still reading it. Listen, but do not
             * write — see the note on s_buffer_in_use. The offline vocabulary
             * still works during this; only the cloud path waits its turn.
             */
            ESP_LOGI(TAG, "Previous recording still in use; not recording this one");
            s_recording = false;
            return;
        }
    }

    s_recording = true;
}

void voice_recorder_set_in_use(bool in_use)
{
    s_buffer_in_use = in_use;
    s_buffer_claimed_us = in_use ? esp_timer_get_time() : 0;
}

int32_t voice_recorder_get_peak_amplitude(void)
{
    return s_gate.peak_amplitude;
}

void voice_recorder_stop(void)
{
    s_recording = false;

    /*
     * Logged at INFO, not DEBUG, and with the measurements in it. These three
     * numbers are the whole explanation for "why did it not answer me" and
     * "why did it answer the television", and they are the only way to tune
     * BUDDY_SPEECH_LOUDNESS_THRESHOLD to a particular room.
     */
    ESP_LOGI(TAG, "Captured %.2f s, %.2f s of it speech (peak %ld, room %lu, needed %lu)",
             voice_recorder_get_duration_seconds(),
             voice_speech_gate_speech_seconds(&s_gate, BUDDY_AUDIO_SAMPLE_RATE_HZ),
             (long)s_gate.peak_amplitude,
             (unsigned long)voice_speech_gate_room_level(),
             (unsigned long)voice_speech_gate_threshold());
}

bool voice_recorder_heard_a_question(void)
{
    return voice_speech_gate_worth_asking(&s_gate, BUDDY_AUDIO_SAMPLE_RATE_HZ);
}

float voice_recorder_get_speech_seconds(void)
{
    return voice_speech_gate_speech_seconds(&s_gate, BUDDY_AUDIO_SAMPLE_RATE_HZ);
}

bool voice_recorder_is_recording(void)
{
    return s_recording;
}

void voice_recorder_append(const int16_t *samples, size_t sample_count)
{
    if (!s_recording || s_samples == NULL || samples == NULL || sample_count == 0) {
        return;
    }

    size_t room = s_capacity_samples - s_recorded_samples;
    if (room == 0) {
        return;   /* Buffer full: keep the beginning, drop the rest. */
    }
    if (sample_count > room) {
        sample_count = room;
    }

    memcpy(&s_samples[s_recorded_samples], samples, sample_count * sizeof(int16_t));
    voice_speech_gate_add(&s_gate, samples, sample_count);
    s_recorded_samples += sample_count;
}

float voice_recorder_get_duration_seconds(void)
{
    return (float)s_recorded_samples / (float)BUDDY_AUDIO_SAMPLE_RATE_HZ;
}

/**
 * @brief Write a little-endian 32-bit value into the header.
 */
static void write_u32(uint8_t *destination, uint32_t value)
{
    destination[0] = (uint8_t)(value & 0xFF);
    destination[1] = (uint8_t)((value >> 8) & 0xFF);
    destination[2] = (uint8_t)((value >> 16) & 0xFF);
    destination[3] = (uint8_t)((value >> 24) & 0xFF);
}

/**
 * @brief Write a little-endian 16-bit value into the header.
 */
static void write_u16(uint8_t *destination, uint16_t value)
{
    destination[0] = (uint8_t)(value & 0xFF);
    destination[1] = (uint8_t)((value >> 8) & 0xFF);
}

esp_err_t voice_recorder_get_wav(const uint8_t **wav_data, size_t *wav_length)
{
    if (wav_data == NULL || wav_length == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    /*
     * The gate, not a bare length check.
     *
     * This used to accept anything longer than a quarter of a second, which
     * meant a cough, a chair, or the television triggering the wake word all
     * bought a transcription. Asking whether there was SPEECH in it is the
     * difference between an assistant and a meter.
     */
    if (s_buffer == NULL || !voice_recorder_heard_a_question()) {
        return ESP_ERR_INVALID_STATE;
    }

    const uint32_t data_bytes   = (uint32_t)(s_recorded_samples * sizeof(int16_t));
    const uint16_t channels     = 1;
    const uint16_t bits         = BUDDY_AUDIO_BITS_PER_SAMPLE;
    const uint32_t sample_rate  = BUDDY_AUDIO_SAMPLE_RATE_HZ;
    const uint16_t block_align  = (uint16_t)(channels * bits / 8);
    const uint32_t byte_rate    = sample_rate * block_align;

    /* Build the 44-byte canonical header in the space reserved for it. */
    uint8_t *header = s_buffer;
    memcpy(&header[0], "RIFF", 4);
    write_u32(&header[4], 36 + data_bytes);      /* Everything after this field */
    memcpy(&header[8], "WAVE", 4);
    memcpy(&header[12], "fmt ", 4);
    write_u32(&header[16], 16);                  /* PCM format chunk is 16 long */
    write_u16(&header[20], 1);                   /* 1 = uncompressed PCM        */
    write_u16(&header[22], channels);
    write_u32(&header[24], sample_rate);
    write_u32(&header[28], byte_rate);
    write_u16(&header[32], block_align);
    write_u16(&header[34], bits);
    memcpy(&header[36], "data", 4);
    write_u32(&header[40], data_bytes);

    *wav_data = s_buffer;
    *wav_length = WAV_HEADER_BYTES + data_bytes;
    return ESP_OK;
}
