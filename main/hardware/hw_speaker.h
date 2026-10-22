/*
 * hw_speaker.h — Everything that comes OUT of the box.
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 *
 * Three kinds of sound share one speaker, so they all funnel through here:
 *
 *   chimes   short WAV files played straight from flash, synchronously
 *   music    MP3 files decoded by the esp-audio-player component
 *   speech   MP3 audio returned by the cloud text-to-speech service
 *
 * On top of playback this module measures how loud the audio it is pushing out
 * happens to be. That number is what makes the buddy's mouth move in time with
 * its own voice — see hw_speaker_get_output_level().
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Called when a track or spoken reply finishes on its own.
 */
typedef void (*hw_speaker_finished_cb_t)(void);

/**
 * @brief Initialise the codec output path and the MP3 player.
 *
 * @note Call after bsp_board_init(), which brings the codec itself up.
 */
esp_err_t hw_speaker_init(void);

/**
 * @brief Register a callback fired when playback reaches the end of a track.
 */
void hw_speaker_set_finished_callback(hw_speaker_finished_cb_t callback);

/**
 * @brief Set the output volume.
 *
 * @param percent  0 (silent) to 100 (loudest).
 */
void hw_speaker_set_volume(uint8_t percent);

/**
 * @brief Read the volume currently applied to the codec.
 */
uint8_t hw_speaker_get_volume(void);

/**
 * @brief Mute or unmute without losing the volume setting.
 */
void hw_speaker_set_muted(bool muted);

/**
 * @brief Play a short WAV file from the filesystem and return when it is done.
 *
 * Used for the wake-up "ding" and other notification chimes, which are short
 * enough that blocking is simpler than juggling another callback.
 *
 * @param path  Absolute path, e.g. "/spiffs/chime_wake.wav".
 */
esp_err_t hw_speaker_play_wav_blocking(const char *path);

/**
 * @brief Start playing an MP3 file and return immediately.
 *
 * @param path  Absolute path to a .mp3 file.
 */
esp_err_t hw_speaker_play_mp3_file(const char *path);

/**
 * @brief Start playing MP3 data already sitting in memory.
 *
 * This is how spoken replies are played: the cloud hands back a block of MP3
 * bytes and there is no reason to write them to flash first.
 *
 * @param data    Pointer to MP3 bytes. Must stay valid until playback finishes.
 * @param length  Number of bytes.
 */
esp_err_t hw_speaker_play_mp3_memory(const void *data, size_t length);

/**
 * @brief Stop whatever is playing right now.
 */
void hw_speaker_stop(void);

/**
 * @brief Pause playback, keeping the position.
 */
void hw_speaker_pause(void);

/**
 * @brief Resume playback after hw_speaker_pause().
 */
void hw_speaker_resume(void);

/**
 * @brief True while any audio is being pushed to the codec.
 */
bool hw_speaker_is_playing(void);

/**
 * @brief How loud the audio leaving the box is right now, 0-100.
 *
 * Computed from the root-mean-square amplitude of the most recent buffer sent
 * to the codec, then smoothed so it decays rather than flickering. The face
 * animator samples this to size the mouth opening.
 */
uint8_t hw_speaker_get_output_level(void);

/**
 * @brief Reset the sample-rate/format of the codec back to the microphone's.
 *
 * Playing a file reconfigures the codec clock for that file. The wake-word
 * engine needs the codec back at 16 kHz stereo afterwards, and this does that.
 */
void hw_speaker_restore_capture_format(void);

#ifdef __cplusplus
}
#endif
