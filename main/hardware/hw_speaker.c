/*
 * hw_speaker.c — Audio output, plus the loudness meter that drives lip sync.
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */

#include <inttypes.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_check.h"
#include "esp_log.h"

#include "audio_player.h"
#include "bsp/esp-bsp.h"
#include "bsp_board.h"

#include "audio_resampler.h"
#include "buddy_config.h"
#include "buddy_settings.h"
#include "hw_speaker.h"

static const char *TAG = "hw_speaker";

/** Bytes read from a WAV file per write when playing a chime. */
#define WAV_CHUNK_BYTES             4096

/**
 * Longest a single chunk write may block for.
 *
 * 4096 bytes at 16 kHz stereo 16-bit is about 64 ms of audio, so half a second
 * is eight times the time it should ever need. See the loop in
 * hw_speaker_play_wav_blocking() for why waiting forever is the wrong answer.
 */
#define WAV_WRITE_TIMEOUT_MS        500

/**
 * Average sample amplitude, out of 32767, that counts as "mouth wide open".
 * Speech and music rarely sit above this, so it makes a lively mouth without
 * pinning it open the whole time. Lower it for a more animated mouth.
 */
#define LOUDNESS_FULL_SCALE         5500

/** Only every Nth sample is measured — plenty accurate, far cheaper. */
#define LOUDNESS_SAMPLE_STRIDE      8

/* ------------------------------------------------------------------------ */
/* State                                                                     */
/* ------------------------------------------------------------------------ */

static volatile uint8_t s_output_level;      /*!< 0-100, smoothed loudness    */
static volatile bool    s_chime_in_progress; /*!< A blocking WAV is playing   */
static uint8_t          s_volume_percent = 70;
static bool             s_initialised;
static hw_speaker_finished_cb_t s_finished_callback;

/* ------------------------------------------------------------------------ */
/* The loudness meter                                                        */
/* ------------------------------------------------------------------------ */

/**
 * @brief Measure one buffer of 16-bit samples and fold it into the level.
 *
 * Mean absolute amplitude is used rather than a true RMS: it costs one add per
 * sample instead of a multiply, and for driving a cartoon mouth the difference
 * is invisible.
 */
static void update_output_level(const void *audio_buffer, size_t byte_count)
{
    const int16_t *samples = (const int16_t *)audio_buffer;
    const size_t sample_count = byte_count / sizeof(int16_t);

    if (samples == NULL || sample_count == 0) {
        return;
    }

    uint32_t total = 0;
    uint32_t measured = 0;
    for (size_t i = 0; i < sample_count; i += LOUDNESS_SAMPLE_STRIDE) {
        int32_t value = samples[i];
        total += (uint32_t)((value < 0) ? -value : value);
        measured++;
    }
    if (measured == 0) {
        return;
    }

    const uint32_t average = total / measured;
    uint32_t level = (average * 100u) / LOUDNESS_FULL_SCALE;
    if (level > 100u) {
        level = 100u;
    }

    /*
     * Snap upwards straight away so consonants read as sharp mouth movements,
     * but ease downwards so the mouth does not chatter between syllables.
     */
    const uint8_t previous = s_output_level;
    s_output_level = (level > previous)
                     ? (uint8_t)level
                     : (uint8_t)(((uint32_t)previous * 3u + level) / 4u);
}

uint8_t hw_speaker_get_output_level(void)
{
    return s_output_level;
}

/* ------------------------------------------------------------------------ */
/* Keeping the microphone at its own rate while audio plays                  */
/* ------------------------------------------------------------------------ */

/*
 * The speaker and the microphone share one I2S clock, so letting a 44.1 kHz
 * track move it takes the microphone with it and leaves the wake word deaf for
 * the length of the song. The codec is therefore pinned at
 * BUDDY_AUDIO_SAMPLE_RATE_HZ for its whole life and anything recorded higher is
 * converted down on the way out. audio_resampler.h has the full explanation and
 * all of the arithmetic; this file just drives it.
 */

/** Output frames converted before each flush to the codec. 1 KB of scratch. */
#define RESAMPLE_CHUNK_FRAMES       256

static audio_resampler_t s_resampler;
static int16_t s_resample_scratch[RESAMPLE_CHUNK_FRAMES * AUDIO_RESAMPLER_MAX_CHANNELS];

/**
 * @brief Convert a buffer down to the codec rate and push it out in chunks.
 *
 * Working a chunk at a time is what keeps the scratch buffer a fixed 1 KB no
 * matter how much the decoder hands over — and on this board, internal RAM is
 * the resource worth being stingy with.
 */
static esp_err_t write_converted(int16_t *frames, size_t frame_count, uint32_t timeout_ms)
{
    const uint8_t channels = s_resampler.channels;
    size_t remaining = frame_count;
    size_t offset = 0;

    while (remaining > 0) {
        const size_t chunk = (remaining < RESAMPLE_CHUNK_FRAMES)
                             ? remaining : RESAMPLE_CHUNK_FRAMES;

        const size_t produced = audio_resampler_process(&s_resampler,
                                                        &frames[offset * channels], chunk,
                                                        s_resample_scratch,
                                                        RESAMPLE_CHUNK_FRAMES);
        if (produced > 0) {
            size_t written = 0;
            esp_err_t err = bsp_i2s_write(s_resample_scratch,
                                          produced * channels * sizeof(int16_t),
                                          &written, timeout_ms);
            if (err != ESP_OK) {
                return err;
            }
        }

        offset += chunk;
        remaining -= chunk;
    }

    return ESP_OK;
}

/* ------------------------------------------------------------------------ */
/* Callbacks handed to the audio player component                            */
/* ------------------------------------------------------------------------ */

/**
 * @brief Every byte the player emits passes through here on its way to the DAC.
 *
 * Sitting in the middle of the stream is what lets the buddy know how loud it
 * is being without any extra plumbing.
 */
static esp_err_t speaker_write_cb(void *audio_buffer, size_t length,
                                  size_t *bytes_written, uint32_t timeout_ms)
{
    update_output_level(audio_buffer, length);

    if (!audio_resampler_is_needed(&s_resampler)) {
        return bsp_i2s_write(audio_buffer, length, bytes_written, timeout_ms);
    }

    /*
     * The player only wants to know that its buffer was consumed, not how many
     * bytes reached the codec — which after conversion is always fewer. Report
     * the full length so it moves on to the next block.
     */
    const size_t frame_bytes = s_resampler.channels * sizeof(int16_t);
    const size_t frame_count = length / frame_bytes;

    esp_err_t err = write_converted((int16_t *)audio_buffer, frame_count, timeout_ms);
    if (bytes_written != NULL) {
        *bytes_written = length;
    }
    return err;
}

/**
 * @brief The player mutes around track boundaries to avoid clicks.
 *
 * The ES8311 codec zeroes its volume register when muted, so the volume has to
 * be re-applied on the way back out.
 */
static esp_err_t speaker_mute_cb(AUDIO_PLAYER_MUTE_SETTING setting)
{
    bsp_codec_mute_set(setting == AUDIO_PLAYER_MUTE);

    if (setting == AUDIO_PLAYER_UNMUTE) {
        bsp_codec_volume_set(s_volume_percent, NULL);
    } else {
        /* Silence means silence — let the mouth close. */
        s_output_level = 0;
    }
    return ESP_OK;
}

/**
 * @brief The player asks for a new sample rate whenever a track starts.
 *
 * This is where the buddy declines. The requested rate is remembered so the
 * write callback can convert down to it, but the codec itself is always set to
 * the one rate the speech models can work with. See audio_resampler.h for why
 * moving it breaks the wake word.
 */
static esp_err_t speaker_clock_cb(uint32_t rate, uint32_t bits_per_sample, i2s_slot_mode_t channels)
{
    const uint8_t channel_count = (channels == I2S_SLOT_MODE_MONO) ? 1 : 2;

    audio_resampler_begin(&s_resampler, rate, BUDDY_AUDIO_SAMPLE_RATE_HZ, channel_count);

    if (rate != BUDDY_AUDIO_SAMPLE_RATE_HZ) {
        ESP_LOGI(TAG, "Track is %" PRIu32 " Hz; playing it at %d Hz so the wake word keeps working",
                 rate, BUDDY_AUDIO_SAMPLE_RATE_HZ);
    }

    esp_err_t err = bsp_codec_set_fs(BUDDY_AUDIO_SAMPLE_RATE_HZ, bits_per_sample, channels);

    /* Toggling mute forces the codec to latch the new clock cleanly. */
    bsp_codec_mute_set(true);
    bsp_codec_mute_set(false);
    bsp_codec_volume_set(s_volume_percent, NULL);
    vTaskDelay(pdMS_TO_TICKS(30));

    return err;
}

/**
 * @brief Track-level events from the player.
 */
static void speaker_event_cb(audio_player_cb_ctx_t *context)
{
    if (context == NULL) {
        return;
    }

    switch (context->audio_event) {
    case AUDIO_PLAYER_CALLBACK_EVENT_IDLE:
        /*
         * Nothing left to play. Put the codec back into the format the
         * microphone wants, drop the mouth shut, and tell whoever was waiting.
         */
        s_output_level = 0;
        hw_speaker_restore_capture_format();
        if (s_finished_callback != NULL) {
            s_finished_callback();
        }
        break;

    case AUDIO_PLAYER_CALLBACK_EVENT_PLAYING:
        ESP_LOGD(TAG, "Playback started");
        break;

    case AUDIO_PLAYER_CALLBACK_EVENT_PAUSE:
        s_output_level = 0;
        break;

    default:
        break;
    }
}

/* ------------------------------------------------------------------------ */
/* Public API                                                                */
/* ------------------------------------------------------------------------ */

esp_err_t hw_speaker_init(void)
{
    if (s_initialised) {
        return ESP_OK;
    }

    s_volume_percent = buddy_settings_get()->speaker_volume;

    const audio_player_config_t config = {
        .mute_fn    = speaker_mute_cb,
        .write_fn   = speaker_write_cb,
        .clk_set_fn = speaker_clock_cb,
        .priority   = 5,
    };

    ESP_RETURN_ON_ERROR(audio_player_new(config), TAG, "audio player failed to start");
    audio_player_callback_register(speaker_event_cb, NULL);

    bsp_codec_volume_set(s_volume_percent, NULL);

    s_initialised = true;
    ESP_LOGI(TAG, "Speaker ready at volume %u%%", s_volume_percent);
    return ESP_OK;
}

void hw_speaker_set_finished_callback(hw_speaker_finished_cb_t callback)
{
    s_finished_callback = callback;
}

void hw_speaker_set_volume(uint8_t percent)
{
    if (percent > 100) {
        percent = 100;
    }
    s_volume_percent = percent;
    bsp_codec_volume_set(percent, NULL);
}

uint8_t hw_speaker_get_volume(void)
{
    return s_volume_percent;
}

void hw_speaker_set_muted(bool muted)
{
    bsp_codec_mute_set(muted);
    if (muted) {
        s_output_level = 0;
    } else {
        bsp_codec_volume_set(s_volume_percent, NULL);
    }
}

/**
 * @brief The 44-byte header at the front of a canonical RIFF/WAVE file.
 */
typedef struct {
    uint8_t  riff_id[4];        /* "RIFF"                                     */
    int32_t  riff_size;
    uint8_t  wave_id[4];        /* "WAVE"                                     */
    uint8_t  format_id[4];      /* "fmt "                                     */
    int32_t  format_size;
    int16_t  audio_format;
    int16_t  channel_count;
    int32_t  sample_rate;
    int32_t  byte_rate;
    int16_t  block_align;
    int16_t  bits_per_sample;
    uint8_t  data_id[4];        /* "data"                                     */
    int32_t  data_size;
} wav_file_header_t;

esp_err_t hw_speaker_play_wav_blocking(const char *path)
{
    if (path == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        ESP_LOGW(TAG, "Chime not found: %s", path);
        return ESP_ERR_NOT_FOUND;
    }

    /*
     * NEVER while the player is running. This is the v3.7.1 crash fix.
     *
     * Two things below are safe on an idle codec and unsafe on a busy one, and
     * it is worth being precise about which, because the obvious explanation is
     * not the real one. The codec is NOT being moved to a different sample
     * rate — speaker_clock_cb keeps it at BUDDY_AUDIO_SAMPLE_RATE_HZ for
     * everything and resamples in software, so a chime and a track are already
     * at the same rate. The hazards are:
     *
     *   1. bsp_codec_set_fs() plus the mute toggle RECONFIGURE the I2S channel.
     *      Doing that while the player's task is blocked inside a write on that
     *      same channel is the classic way to take an ESP32 down.
     *   2. bsp_i2s_write() below then runs from THIS task while the player's
     *      write callback runs from its own — two unsynchronised writers on one
     *      channel, plus a shared s_output_level between them.
     *
     * That is what "sing me a song sometimes crashes" was: the command handler
     * starts the track, then chimes at it. It looked like one track's fault
     * because the track is picked at random, so whichever came up got blamed.
     *
     * The caller is fixed too (see handle_recognised_command), but the guard
     * belongs here as well, because this is where the damage is done and every
     * future caller gets it for free. Refusing beats stopping the player: a
     * missing ding is nothing, and a chime is never more important than the
     * audio already playing.
     */
    if (audio_player_get_state() == AUDIO_PLAYER_STATE_PLAYING) {
        ESP_LOGD(TAG, "Skipping %s; the player already has the codec", path);
        fclose(file);
        return ESP_ERR_INVALID_STATE;
    }

    wav_file_header_t header;
    if (fread(&header, 1, sizeof(header), file) != sizeof(header)) {
        ESP_LOGW(TAG, "Chime too short to hold a header: %s", path);
        fclose(file);
        return ESP_ERR_INVALID_SIZE;
    }

    if (memcmp(header.riff_id, "RIFF", 4) != 0 || memcmp(header.wave_id, "WAVE", 4) != 0) {
        ESP_LOGW(TAG, "Not a WAV file: %s", path);
        fclose(file);
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t *chunk = malloc(WAV_CHUNK_BYTES);
    if (chunk == NULL) {
        fclose(file);
        return ESP_ERR_NO_MEM;
    }

    /*
     * Chimes are authored at BUDDY_AUDIO_SAMPLE_RATE_HZ precisely so this path
     * never has to move the codec clock — moving it would take the microphone
     * with it and leave the wake word deaf, exactly as described above
     * audio_resampler.h. A chime at any other rate will play at the wrong pitch,
     * which is a far better failure than a buddy that stops listening.
     */
    if (header.sample_rate != BUDDY_AUDIO_SAMPLE_RATE_HZ) {
        ESP_LOGW(TAG, "%s is %" PRId32 " Hz, not %d Hz — it will sound wrong. Re-export it.",
                 path, header.sample_rate, BUDDY_AUDIO_SAMPLE_RATE_HZ);
    }

    bsp_codec_set_fs(BUDDY_AUDIO_SAMPLE_RATE_HZ,
                     (uint32_t)header.bits_per_sample,
                     I2S_SLOT_MODE_STEREO);
    bsp_codec_mute_set(true);
    bsp_codec_mute_set(false);
    bsp_codec_volume_set(BUDDY_CHIME_VOLUME, NULL);
    vTaskDelay(pdMS_TO_TICKS(30));

    s_chime_in_progress = true;

    /*
     * A finite timeout, not portMAX_DELAY.
     *
     * A chime is at most a second of audio, so a write that has not completed
     * in half a second means the codec has stopped draining — its clock
     * reconfigured underneath us, or the channel disabled by something else.
     * Waiting forever for it does not fix that; it just parks whichever task is
     * playing the chime, permanently. Giving up leaves the buddy with one
     * truncated ding instead of one dead task.
     */
    size_t read_bytes;
    while ((read_bytes = fread(chunk, 1, WAV_CHUNK_BYTES, file)) > 0) {
        size_t written = 0;
        update_output_level(chunk, read_bytes);

        if (bsp_i2s_write(chunk, read_bytes, &written, WAV_WRITE_TIMEOUT_MS) != ESP_OK) {
            ESP_LOGW(TAG, "The codec stopped accepting audio; cutting %s short", path);
            break;
        }
    }

    /* Let the last samples drain out of the codec before we change anything. */
    vTaskDelay(pdMS_TO_TICKS(20));
    s_chime_in_progress = false;
    s_output_level = 0;

    free(chunk);
    fclose(file);

    bsp_codec_volume_set(s_volume_percent, NULL);
    return ESP_OK;
}

esp_err_t hw_speaker_play_mp3_file(const char *path)
{
    if (path == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        ESP_LOGW(TAG, "Track not found: %s", path);
        return ESP_ERR_NOT_FOUND;
    }

    /*
     * One thing at a time on the speaker. Handing the player a second track
     * while the first is still running leaves two decodes racing for one
     * channel, which sounds like a fault and can behave like one.
     */
    if (audio_player_get_state() == AUDIO_PLAYER_STATE_PLAYING) {
        audio_player_stop();
    }

    /*
     * The player owns the handle from here and closes it when the track ends —
     * but ONLY if it accepted it. On a refusal the handle is still ours, and
     * leaking it is not the small mistake it looks like: SPIFFS is mounted with
     * a handful of file slots, so a few refused tracks exhaust them and every
     * later fopen returns NULL. Music and speech both stop, and nothing in the
     * log says why.
     */
    const esp_err_t err = audio_player_play(file);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "The player refused %s (%s)", path, esp_err_to_name(err));
        fclose(file);
    }
    return err;
}

esp_err_t hw_speaker_play_mp3_memory(const void *data, size_t length)
{
    if (data == NULL || length == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    /*
     * fmemopen wraps a block of RAM in a FILE handle, so the MP3 decoder can
     * read from memory using exactly the same code path as a file on flash.
     */
    FILE *file = fmemopen((void *)data, length, "rb");
    if (file == NULL) {
        ESP_LOGE(TAG, "Could not wrap %u bytes of audio for playback", (unsigned)length);
        return ESP_ERR_NO_MEM;
    }

    if (audio_player_get_state() == AUDIO_PLAYER_STATE_PLAYING) {
        audio_player_stop();
    }

    /* Same ownership rule as above: ours again if the player would not take it. */
    const esp_err_t err = audio_player_play(file);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "The player refused %u bytes of audio (%s)",
                 (unsigned)length, esp_err_to_name(err));
        fclose(file);
    }
    return err;
}

void hw_speaker_stop(void)
{
    audio_player_stop();
    s_output_level = 0;
}

void hw_speaker_pause(void)
{
    if (audio_player_get_state() == AUDIO_PLAYER_STATE_PLAYING) {
        audio_player_pause();
    }
    s_output_level = 0;
}

void hw_speaker_resume(void)
{
    if (audio_player_get_state() == AUDIO_PLAYER_STATE_PAUSE) {
        audio_player_resume();
    }
}

bool hw_speaker_is_playing(void)
{
    return s_chime_in_progress ||
           (audio_player_get_state() == AUDIO_PLAYER_STATE_PLAYING);
}

void hw_speaker_restore_capture_format(void)
{
    /*
     * Belt and braces. Nothing in this file moves the clock away from the
     * capture rate any more, but a future codec call — or a BSP update — might,
     * and a microphone left at the wrong rate is a silent failure: the buddy
     * looks perfectly alive and simply never answers again.
     */
    audio_resampler_begin(&s_resampler, BUDDY_AUDIO_SAMPLE_RATE_HZ,
                          BUDDY_AUDIO_SAMPLE_RATE_HZ, 2);

    bsp_codec_set_fs(BUDDY_AUDIO_SAMPLE_RATE_HZ,
                     BUDDY_AUDIO_BITS_PER_SAMPLE,
                     I2S_SLOT_MODE_STEREO);
}
