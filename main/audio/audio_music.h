/*
 * audio_music.h — The buddy's jukebox.
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 *
 * Two sources of music, chosen in Settings:
 *
 *   FLASH   .mp3 files baked into the SPIFFS storage partition. Instant, works
 *           with no network, and survives a house move. Drop files into
 *           examples/factory_demo/spiffs/music/ and reflash to change them.
 *
 *   STREAM  an .mp3 fetched over Wi-Fi from a URL — a file on your computer, a
 *           radio stream, whatever you point it at. The track is pulled into
 *           PSRAM first and then played, which trades a few seconds of wait for
 *           playback that never stutters when the network hiccups.
 *
 * A note on Bluetooth, since it is the obvious question: the ESP32-S3 has Wi-Fi
 * and Bluetooth LE only. It has no Bluetooth Classic radio, so it physically
 * cannot receive A2DP audio from a phone. Streaming over Wi-Fi is the way to
 * play something from another device.
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Scan the music folder and get ready to play.
 */
esp_err_t audio_music_init(void);

/**
 * @brief Re-scan the music folder.
 *
 * @return How many playable tracks were found.
 */
size_t audio_music_rescan(void);

/**
 * @brief How many tracks the playlist holds.
 */
size_t audio_music_get_track_count(void);

/**
 * @brief Start a randomly chosen track.
 *
 * @param name_out    Receives the track name, for display. May be NULL.
 * @param name_size   Size of @p name_out.
 * @return ESP_OK, or ESP_ERR_NOT_FOUND when there is nothing to play.
 */
esp_err_t audio_music_play_random(char *name_out, size_t name_size);

/**
 * @brief Move to the next track in the playlist and play it.
 */
esp_err_t audio_music_play_next(char *name_out, size_t name_size);

/**
 * @brief Stop playback.
 */
void audio_music_stop(void);

/**
 * @brief Pause playback, remembering the position.
 */
void audio_music_pause(void);

/**
 * @brief Resume after audio_music_pause().
 */
void audio_music_resume(void);

/**
 * @brief True while a track is playing.
 */
bool audio_music_is_playing(void);

/**
 * @brief The name of the track currently loaded, or an empty string.
 */
const char *audio_music_get_current_track(void);

#ifdef __cplusplus
}
#endif
