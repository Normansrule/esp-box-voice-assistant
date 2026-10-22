/*
 * audio_voice.c — The buddy's offline voice.
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 *
 * See audio_voice.h for what this is and how a reply finds its clip.
 */

#include <dirent.h>
#include <stdio.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"

#include "audio_voice.h"
#include "audio_voice_hash.h"
#include "hw_speaker.h"

static const char *TAG = "voice_clips";

/** Longest path we build: the folder, a slash, eight hex digits, ".mp3". */
#define VOICE_PATH_MAX_LEN          (sizeof(AUDIO_VOICE_FOLDER) + 16)

/**
 * Room for one assembled sentence.
 *
 * The longest the buddy assembles is six fragments, and the largest fragment on
 * flash is under 24 KB, so 96 KB is roughly double the worst case. It lives in
 * PSRAM, where there are megabytes spare — putting it in internal RAM would be
 * taking from the one pool that actually runs out.
 */
#define ASSEMBLED_SENTENCE_MAX_BYTES    (96 * 1024)

static uint16_t s_clip_count;

/**
 * The buffer an assembled sentence is decoded from.
 *
 * Held for the lifetime of the program rather than allocated per sentence,
 * because the speaker decodes straight out of it while playing — freeing it at
 * the end of this function would pull the audio out from under the decoder.
 * Reuse is safe because the caller holds VOICE_HOLD_SPEAKING for the whole of
 * playback, so a second sentence cannot start while this one is being read.
 */
static uint8_t *s_sentence_buffer;

/**
 * @brief Does this file exist?
 *
 * Opening it is the long way round, and it is deliberate. The obvious call —
 * access(path, F_OK) — DOES NOT WORK ON SPIFFS. ESP-IDF routes access() through
 * its VFS layer to a per-filesystem handler, and the SPIFFS driver does not
 * register one, so every call fails with ENOSYS no matter what is on flash.
 *
 * The failure is silent and total: it looks exactly like the file being
 * missing. It made the buddy mute on a board that had all 47 clips sitting in
 * its storage partition, and made the chimes report as absent at the same time.
 * fopen() is implemented, so this asks the question in a way SPIFFS can
 * actually answer.
 */
static bool file_is_present(const char *path)
{
    FILE *file = fopen(path, "rb");

    if (file == NULL) {
        return false;
    }

    fclose(file);
    return true;
}

/**
 * @brief Work out where the clip for @p text would live.
 *
 * Note this says nothing about whether the file exists — that is the caller's
 * question, and answering it costs a filesystem lookup we only want to do once.
 */
static void path_for_text(const char *text, char *destination, size_t size)
{
    snprintf(destination, size, AUDIO_VOICE_FOLDER "/%08lx.mp3",
             (unsigned long)audio_voice_hash(text));
}

esp_err_t audio_voice_init(void)
{
    s_clip_count = 0;

    /*
     * PSRAM, and not fatal if it fails. Without this buffer the buddy loses the
     * assembled sentences — the time, the temperature — and keeps every reply
     * that has a recording of its own, which is most of them.
     */
    if (s_sentence_buffer == NULL) {
        s_sentence_buffer = heap_caps_malloc(ASSEMBLED_SENTENCE_MAX_BYTES,
                                             MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (s_sentence_buffer == NULL) {
            ESP_LOGW(TAG, "No PSRAM for assembled sentences; numbers will be shown, not said");
        }
    }

    DIR *folder = opendir(AUDIO_VOICE_FOLDER);
    if (folder == NULL) {
        /*
         * Not an error worth failing a boot over. The buddy simply has no
         * offline voice — every reply still appears in the speech bubble, and
         * the online voice still works if it is configured.
         */
        ESP_LOGW(TAG, "No %s folder — the buddy will be silent without Wi-Fi",
                 AUDIO_VOICE_FOLDER);
        return ESP_ERR_NOT_FOUND;
    }

    const struct dirent *entry;
    while ((entry = readdir(folder)) != NULL) {
        const char *dot = strrchr(entry->d_name, '.');
        if (dot != NULL && strcmp(dot, ".mp3") == 0) {
            s_clip_count++;
        }
    }
    closedir(folder);

    ESP_LOGI(TAG, "%u spoken replies on flash", (unsigned)s_clip_count);
    return ESP_OK;
}

uint16_t audio_voice_get_clip_count(void)
{
    return s_clip_count;
}

bool audio_voice_speak(const char *text)
{
    if (text == NULL || text[0] == '\0' || s_clip_count == 0) {
        return false;
    }

    char path[VOICE_PATH_MAX_LEN];
    path_for_text(text, path, sizeof(path));

    if (!file_is_present(path)) {
        /*
         * A reply with no clip. Expected for anything assembled at runtime —
         * the time, a temperature, a track name — and it fires at most once per
         * spoken command, so it earns a visible line. Saying WHICH words had no
         * recording is the difference between diagnosing a silent buddy in one
         * reading of the log and guessing at it.
         */
        ESP_LOGI(TAG, "No clip for \"%s\" (looked for %s)", text, path);
        return false;
    }

    if (hw_speaker_play_mp3_file(path) != ESP_OK) {
        ESP_LOGW(TAG, "Could not play %s", path);
        return false;
    }

    return true;
}

/* ------------------------------------------------------------------------ */
/* Saying a sentence that was assembled from fragments                       */
/* ------------------------------------------------------------------------ */

/**
 * @brief Append one clip's bytes to the sentence buffer.
 *
 * @return Bytes appended, or 0 if the clip is missing or would not fit.
 */
static size_t append_clip(const char *text, size_t offset)
{
    char path[VOICE_PATH_MAX_LEN];
    path_for_text(text, path, sizeof(path));

    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        ESP_LOGW(TAG, "No recording of \"%s\" (looked for %s)", text, path);
        return 0;
    }

    const size_t room = ASSEMBLED_SENTENCE_MAX_BYTES - offset;
    const size_t read_bytes = fread(&s_sentence_buffer[offset], 1, room, file);
    fclose(file);

    return read_bytes;
}

bool audio_voice_speak_parts(const char *const *parts, size_t count)
{
    if (parts == NULL || count == 0 || s_sentence_buffer == NULL || s_clip_count == 0) {
        return false;
    }

    /*
     * Gather every fragment BEFORE playing any of it.
     *
     * An assembled sentence is only true as a whole. If the recording of the
     * number is missing, saying "it is" and then stopping is worse than staying
     * quiet and letting the caption speak for itself — so a single miss
     * abandons the entire utterance.
     */
    size_t total = 0;

    for (size_t i = 0; i < count; i++) {
        const size_t appended = append_clip(parts[i], total);
        if (appended == 0) {
            ESP_LOGI(TAG, "Abandoning the spoken sentence; part %u has no recording",
                     (unsigned)i);
            return false;
        }
        total += appended;
    }

    /*
     * One decode, not one per fragment.
     *
     * MP3 frames are self-contained and every clip here was encoded at the same
     * rate and bitrate, so the concatenation is a valid MP3 stream. Handing the
     * decoder one stream is what makes this sound like a sentence: playing the
     * files one after another would put a gap at every join while the next file
     * was opened, and "it is ... three ... forty seven" is not how anyone says
     * the time.
     */
    if (hw_speaker_play_mp3_memory(s_sentence_buffer, total) != ESP_OK) {
        ESP_LOGW(TAG, "Could not play the assembled sentence");
        return false;
    }

    ESP_LOGI(TAG, "Speaking %u fragments, %u bytes", (unsigned)count, (unsigned)total);
    return true;
}

bool audio_voice_speak_reply(const voice_reply_t *reply)
{
    if (reply == NULL) {
        return false;
    }

    /*
     * A recording of the whole sentence wins when there is one. It is a single
     * take by the same voice, so it always sounds better than the same words
     * assembled from pieces — assembly is the fallback for sentences that could
     * never have been recorded, not a replacement for the ones that were.
     */
    if (audio_voice_speak(reply->text)) {
        return true;
    }

    if (voice_speech_plan_is_usable(&reply->speech)) {
        return audio_voice_speak_parts(reply->speech.parts, reply->speech.count);
    }

    return false;
}
