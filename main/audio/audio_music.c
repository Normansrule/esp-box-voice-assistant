/*
 * audio_music.c — Playlist management and streaming.
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_random.h"

#include "audio_music.h"
#include "buddy_config.h"
#include "buddy_settings.h"
#include "hw_speaker.h"
#include "net_wifi.h"

static const char *TAG = "music";

/** Longest track filename we keep, including the extension. */
#define TRACK_NAME_MAX_LEN      64

/** Bytes pulled from the network per read while downloading a stream. */
#define STREAM_READ_CHUNK       4096

/* ------------------------------------------------------------------------ */
/* State                                                                     */
/* ------------------------------------------------------------------------ */

static char   s_track_names[BUDDY_MUSIC_MAX_TRACKS][TRACK_NAME_MAX_LEN];
static size_t s_track_count;
static size_t s_current_track;
static char   s_now_playing[TRACK_NAME_MAX_LEN];

/** Buffer a streamed track is downloaded into before it is played. */
static uint8_t *s_stream_buffer;
static size_t   s_stream_length;
static volatile bool s_stream_download_running;

/* ------------------------------------------------------------------------ */
/* Scanning the flash playlist                                               */
/* ------------------------------------------------------------------------ */

/**
 * @brief True when the filename ends in ".mp3", ignoring case.
 */
static bool is_mp3_filename(const char *name)
{
    const char *dot = strrchr(name, '.');
    return (dot != NULL) && (strcasecmp(dot, ".mp3") == 0);
}

size_t audio_music_rescan(void)
{
    s_track_count = 0;
    s_current_track = 0;

    DIR *directory = opendir(BUDDY_MUSIC_FOLDER);
    if (directory == NULL) {
        ESP_LOGW(TAG, "No music folder at %s", BUDDY_MUSIC_FOLDER);
        return 0;
    }

    struct dirent *entry;
    while ((entry = readdir(directory)) != NULL && s_track_count < BUDDY_MUSIC_MAX_TRACKS) {
        if (!is_mp3_filename(entry->d_name)) {
            continue;
        }
        strncpy(s_track_names[s_track_count], entry->d_name, TRACK_NAME_MAX_LEN - 1);
        s_track_names[s_track_count][TRACK_NAME_MAX_LEN - 1] = '\0';
        s_track_count++;
    }
    closedir(directory);

    ESP_LOGI(TAG, "Playlist has %u track(s)", (unsigned)s_track_count);
    return s_track_count;
}

esp_err_t audio_music_init(void)
{
    audio_music_rescan();
    return ESP_OK;
}

size_t audio_music_get_track_count(void)
{
    return s_track_count;
}

const char *audio_music_get_current_track(void)
{
    return s_now_playing;
}

/* ------------------------------------------------------------------------ */
/* Playing from flash                                                        */
/* ------------------------------------------------------------------------ */

/**
 * @brief Play the track at @p index of the playlist.
 */
static esp_err_t play_flash_track(size_t index, char *name_out, size_t name_size)
{
    if (index >= s_track_count) {
        return ESP_ERR_NOT_FOUND;
    }

    char full_path[128];
    snprintf(full_path, sizeof(full_path), "%s/%s", BUDDY_MUSIC_FOLDER, s_track_names[index]);

    const esp_err_t err = hw_speaker_play_mp3_file(full_path);
    if (err != ESP_OK) {
        return err;
    }

    s_current_track = index;
    strncpy(s_now_playing, s_track_names[index], sizeof(s_now_playing) - 1);
    s_now_playing[sizeof(s_now_playing) - 1] = '\0';

    /* Trim the ".mp3" for anything shown to the user. */
    char *extension = strrchr(s_now_playing, '.');
    if (extension != NULL) {
        *extension = '\0';
    }

    if (name_out != NULL && name_size > 0) {
        strncpy(name_out, s_now_playing, name_size - 1);
        name_out[name_size - 1] = '\0';
    }

    ESP_LOGI(TAG, "Playing %s", s_now_playing);
    return ESP_OK;
}

/* ------------------------------------------------------------------------ */
/* Playing from a stream                                                     */
/* ------------------------------------------------------------------------ */

/**
 * @brief Download the configured URL into PSRAM, then hand it to the player.
 *
 * Downloading the whole track before playing keeps the decoder fed even when
 * the network stalls, which matters far more on a desk toy than the few seconds
 * of delay it costs.
 */
static void stream_download_task(void *argument)
{
    (void)argument;

    const buddy_settings_t *settings = buddy_settings_get();

    esp_http_client_config_t config = {
        .url = settings->stream_url,
        .timeout_ms = 10000,
        .crt_bundle_attach = esp_crt_bundle_attach,  /* Lets https URLs work too. */
        .buffer_size = STREAM_READ_CHUNK,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        ESP_LOGE(TAG, "Could not create the HTTP client");
        goto finished;
    }

    if (esp_http_client_open(client, 0) != ESP_OK) {
        ESP_LOGE(TAG, "Could not reach %s", settings->stream_url);
        goto finished;
    }

    const int content_length = esp_http_client_fetch_headers(client);
    ESP_LOGI(TAG, "Streaming %s (%d bytes announced)", settings->stream_url, content_length);

    /* Allocate for the announced size, or the cap when the server is coy. */
    size_t capacity = (content_length > 0)
                      ? (size_t)content_length
                      : (size_t)BUDDY_STREAM_MAX_TRACK_BYTES;
    if (capacity > BUDDY_STREAM_MAX_TRACK_BYTES) {
        capacity = BUDDY_STREAM_MAX_TRACK_BYTES;
    }

    free(s_stream_buffer);
    s_stream_buffer = heap_caps_malloc(capacity, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (s_stream_buffer == NULL) {
        ESP_LOGE(TAG, "No PSRAM for a %u byte track", (unsigned)capacity);
        goto finished;
    }

    s_stream_length = 0;
    while (s_stream_length < capacity) {
        const int read = esp_http_client_read(client,
                                              (char *)(s_stream_buffer + s_stream_length),
                                              (int)(capacity - s_stream_length));
        if (read <= 0) {
            break;   /* End of the track, or the connection dropped. */
        }
        s_stream_length += (size_t)read;
    }

    ESP_LOGI(TAG, "Downloaded %u bytes", (unsigned)s_stream_length);

    if (s_stream_length > 1024) {
        hw_speaker_play_mp3_memory(s_stream_buffer, s_stream_length);
        strncpy(s_now_playing, "stream", sizeof(s_now_playing) - 1);
    } else {
        ESP_LOGE(TAG, "Stream was too short to be music");
    }

finished:
    if (client != NULL) {
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
    }
    s_stream_download_running = false;
    vTaskDelete(NULL);
}

/**
 * @brief Kick off a streamed track if the settings and network allow it.
 */
static esp_err_t try_stream(char *name_out, size_t name_size)
{
    const buddy_settings_t *settings = buddy_settings_get();

    if (settings->music_source != BUDDY_MUSIC_SOURCE_STREAM) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    if (settings->stream_url[0] == '\0') {
        ESP_LOGW(TAG, "Streaming is selected but no address is set");
        return ESP_ERR_INVALID_STATE;
    }
    if (!net_wifi_is_connected()) {
        ESP_LOGW(TAG, "Streaming needs Wi-Fi");
        return ESP_ERR_INVALID_STATE;
    }
    if (s_stream_download_running) {
        return ESP_ERR_INVALID_STATE;
    }

    s_stream_download_running = true;

    const BaseType_t created = xTaskCreate(stream_download_task, "music_stream",
                                           5 * 1024, NULL, 4, NULL);
    if (created != pdPASS) {
        s_stream_download_running = false;
        return ESP_FAIL;
    }

    if (name_out != NULL && name_size > 0) {
        strncpy(name_out, "a track from your network", name_size - 1);
        name_out[name_size - 1] = '\0';
    }
    return ESP_OK;
}

/* ------------------------------------------------------------------------ */
/* Public playback API                                                       */
/* ------------------------------------------------------------------------ */

esp_err_t audio_music_play_random(char *name_out, size_t name_size)
{
    /* Streaming first when it is the chosen source; flash is the fallback. */
    if (try_stream(name_out, name_size) == ESP_OK) {
        return ESP_OK;
    }

    if (s_track_count == 0) {
        audio_music_rescan();
    }
    if (s_track_count == 0) {
        return ESP_ERR_NOT_FOUND;
    }

    size_t choice = esp_random() % s_track_count;

    /* Avoid repeating the track that just played, when there is a choice. */
    if (s_track_count > 1 && choice == s_current_track) {
        choice = (choice + 1) % s_track_count;
    }

    return play_flash_track(choice, name_out, name_size);
}

esp_err_t audio_music_play_next(char *name_out, size_t name_size)
{
    if (s_track_count == 0) {
        audio_music_rescan();
    }
    if (s_track_count == 0) {
        return ESP_ERR_NOT_FOUND;
    }

    const size_t next = (s_current_track + 1) % s_track_count;
    return play_flash_track(next, name_out, name_size);
}

void audio_music_stop(void)
{
    hw_speaker_stop();
    s_now_playing[0] = '\0';
}

void audio_music_pause(void)
{
    hw_speaker_pause();
}

void audio_music_resume(void)
{
    hw_speaker_resume();
}

bool audio_music_is_playing(void)
{
    return hw_speaker_is_playing();
}
