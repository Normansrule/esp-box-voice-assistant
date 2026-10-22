/*
 * buddy_selftest.c — The boot-time health check.
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 *
 * Each check is deliberately small and reports a specific, actionable detail
 * rather than a bare pass or fail. "Sensor dock: not attached" tells you what
 * to do; "sensors: FAIL" does not.
 */

#include <dirent.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"

#include "audio_music.h"
#include "audio_prompts.h"
#include "audio_voice.h"
#include "buddy_api_key.h"
#include "buddy_config.h"
#include "buddy_selftest.h"
#include "buddy_settings.h"
#include "hw_microphone.h"
#include "hw_sensors.h"
#include "net_time.h"
#include "net_wifi.h"
#include "voice_wakeword.h"

static const char *TAG = "selftest";

/*
 * Thresholds. These are not arbitrary: below the "failed" figures the firmware
 * genuinely stops working, and below the "warning" figures it starts becoming
 * unreliable under load — a cloud request plus a song playing is the worst case.
 */
#define PSRAM_FAILED_BELOW_BYTES        (1 * 1024 * 1024)
#define PSRAM_WARNING_BELOW_BYTES       (3 * 1024 * 1024)

/*
 * Internal RAM is the one that bites, because PSRAM cannot stand in for it.
 *
 * The failure figure is set from a real crash rather than a guess: at 29 KB
 * free, opening the settings screen used enough of the remainder that
 * i2c_cmd_link_create() started returning NULL. That prints
 *
 *     E i2c: i2c_master_start(1300): i2c command link error
 *
 * once a second, and ends in an abort inside the AHT20 temperature driver,
 * which asserts that its reads succeed. So anything under ~35 KB should be
 * treated as already broken, not as "getting tight".
 */
#define INTERNAL_FAILED_BELOW_BYTES     (35 * 1024)
#define INTERNAL_WARNING_BELOW_BYTES    (55 * 1024)

/* ------------------------------------------------------------------------ */
/* Building a report                                                         */
/* ------------------------------------------------------------------------ */

/**
 * @brief Append one result to the report.
 */
static void add_check(buddy_selftest_report_t *report, const char *name,
                      buddy_check_result_t result, const char *detail_format, ...)
{
    if (report->check_count >= BUDDY_SELFTEST_MAX_CHECKS) {
        return;
    }

    buddy_check_t *check = &report->checks[report->check_count];

    strncpy(check->name, name, sizeof(check->name) - 1);
    check->name[sizeof(check->name) - 1] = '\0';
    check->result = result;

    va_list arguments;
    va_start(arguments, detail_format);
    vsnprintf(check->detail, sizeof(check->detail), detail_format, arguments);
    va_end(arguments);

    report->check_count++;

    switch (result) {
    case BUDDY_CHECK_PASSED:  report->passed_count++;  break;
    case BUDDY_CHECK_WARNING: report->warning_count++; break;
    default:                  report->failed_count++;  break;
    }
}

/* ------------------------------------------------------------------------ */
/* The individual checks                                                     */
/* ------------------------------------------------------------------------ */

static void check_memory(buddy_selftest_report_t *report)
{
    const size_t psram_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    const size_t internal_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);

    buddy_check_result_t psram_result = BUDDY_CHECK_PASSED;
    if (psram_free < PSRAM_FAILED_BELOW_BYTES) {
        psram_result = BUDDY_CHECK_FAILED;
    } else if (psram_free < PSRAM_WARNING_BELOW_BYTES) {
        psram_result = BUDDY_CHECK_WARNING;
    }
    add_check(report, "PSRAM", psram_result, "%u KB free", (unsigned)(psram_free / 1024));

    buddy_check_result_t internal_result = BUDDY_CHECK_PASSED;
    if (internal_free < INTERNAL_FAILED_BELOW_BYTES) {
        internal_result = BUDDY_CHECK_FAILED;
    } else if (internal_free < INTERNAL_WARNING_BELOW_BYTES) {
        internal_result = BUDDY_CHECK_WARNING;
    }
    add_check(report, "Internal RAM", internal_result, "%u KB free",
              (unsigned)(internal_free / 1024));
}

static void check_filesystem(buddy_selftest_report_t *report)
{
    DIR *directory = opendir(BUDDY_PROMPT_FOLDER);

    if (directory == NULL) {
        add_check(report, "Filesystem", BUDDY_CHECK_FAILED,
                  "%s did not mount", BUDDY_PROMPT_FOLDER);
        return;
    }
    closedir(directory);

    add_check(report, "Filesystem", BUDDY_CHECK_PASSED, "mounted at %s", BUDDY_PROMPT_FOLDER);
}

static void check_sounds(buddy_selftest_report_t *report)
{
    const uint8_t chimes_found = audio_prompts_get_available_count();

    if (chimes_found == 0) {
        add_check(report, "Chimes", BUDDY_CHECK_FAILED,
                  "none found - reflash the storage partition");
    } else if (chimes_found < AUDIO_PROMPT_COUNT) {
        add_check(report, "Chimes", BUDDY_CHECK_WARNING,
                  "%u of %d present", chimes_found, (int)AUDIO_PROMPT_COUNT);
    } else {
        add_check(report, "Chimes", BUDDY_CHECK_PASSED, "all %d present", (int)AUDIO_PROMPT_COUNT);
    }

    /*
     * The offline voice. Zero clips is not a failure — the buddy still shows
     * every answer in its speech bubble and still speaks through the online
     * voice — but it IS the difference between a buddy that talks back on a
     * bare desk and one that only mimes, so it is worth saying out loud.
     */
    const uint16_t clips = audio_voice_get_clip_count();
    if (clips == 0) {
        add_check(report, "Offline voice", BUDDY_CHECK_WARNING,
                  "no clips - run scripts/make_voice_clips.py");
    } else {
        add_check(report, "Offline voice", BUDDY_CHECK_PASSED,
                  "%u spoken replies", (unsigned)clips);
    }

    const size_t tracks = audio_music_get_track_count();
    if (tracks == 0) {
        add_check(report, "Music", BUDDY_CHECK_WARNING, "no tracks in %s", BUDDY_MUSIC_FOLDER);
    } else {
        add_check(report, "Music", BUDDY_CHECK_PASSED, "%u track(s) on flash", (unsigned)tracks);
    }
}

static void check_sensors(buddy_selftest_report_t *report)
{
    if (!hw_sensors_is_dock_present()) {
        add_check(report, "Sensor dock", BUDDY_CHECK_WARNING, "not attached");
        return;
    }

    const hw_sensor_reading_t reading = hw_sensors_read_environment();
    if (!reading.valid) {
        add_check(report, "Sensor dock", BUDDY_CHECK_WARNING,
                  "attached, no reading yet");
        return;
    }

    add_check(report, "Sensor dock", BUDDY_CHECK_PASSED, "%.1f C, %.0f%% humidity",
              reading.temperature_celsius, reading.humidity_percent);
}

static void check_voice(buddy_selftest_report_t *report)
{
    if (!voice_wakeword_is_running()) {
        add_check(report, "Speech engine", BUDDY_CHECK_FAILED,
                  "not running - check the model partition");
    } else {
        const uint16_t phrases = voice_wakeword_get_phrase_count();
        if (phrases == 0) {
            add_check(report, "Speech engine", BUDDY_CHECK_FAILED, "no phrases registered");
        } else {
            add_check(report, "Speech engine", BUDDY_CHECK_PASSED,
                      "listening, %u phrases", phrases);
        }
    }

    if (hw_microphone_is_muted()) {
        add_check(report, "Microphone", BUDDY_CHECK_WARNING,
                  "muted by the switch on top");
    } else {
        add_check(report, "Microphone", BUDDY_CHECK_PASSED, "live");
    }
}

static void check_network(buddy_selftest_report_t *report)
{
    switch (net_wifi_get_state()) {
    case NET_WIFI_CONNECTED:
        add_check(report, "Wi-Fi", BUDDY_CHECK_PASSED, "%s (%s)",
                  net_wifi_get_ssid(), net_wifi_get_ip_address());
        break;
    case NET_WIFI_CONNECTING:
        add_check(report, "Wi-Fi", BUDDY_CHECK_WARNING, "still connecting");
        break;
    case NET_WIFI_FAILED:
        add_check(report, "Wi-Fi", BUDDY_CHECK_WARNING, "%s", net_wifi_get_error());
        break;
    default:
        add_check(report, "Wi-Fi", BUDDY_CHECK_WARNING, "not set up yet");
        break;
    }

    /*
     * The clock, which matters far more than it looks like it should.
     *
     * Every cloud request goes over HTTPS, and verifying a certificate means
     * checking that today's date falls inside its validity window. The chip
     * boots at 1 January 1970, which is before every certificate in existence
     * was issued, so until the network time client answers, the handshake fails
     * — with a TLS error that mentions nothing about time.
     */
    if (net_wifi_get_state() != NET_WIFI_CONNECTED) {
        add_check(report, "Clock", BUDDY_CHECK_WARNING, "waiting for Wi-Fi");
    } else if (net_time_is_synchronised()) {
        add_check(report, "Clock", BUDDY_CHECK_PASSED, "set from the network");
    } else {
        add_check(report, "Clock", BUDDY_CHECK_WARNING, "not set yet - HTTPS will fail");
    }

    /*
     * Report the key's LENGTH, not just its presence.
     *
     * "key saved" was actively misleading: a key cut short by a too-small
     * buffer is saved just as happily as a whole one, and then fails every
     * request. A length is something you can compare against what the OpenAI
     * console showed you — a project key is around 165 characters — so a
     * truncated one is obvious at a glance rather than after an hour.
     */
    const buddy_settings_t *settings = buddy_settings_get();

    /*
     * In Home AI mode the OpenAI key is irrelevant, so report the home brain
     * instead. Whether that computer is on is not knowable at boot without a
     * network round trip, so this checks only that it has been set up.
     */
    if (settings->brain_mode == BUDDY_BRAIN_HOME_AI) {
        if (buddy_settings_has_home_brain()) {
            add_check(report, "Home AI", BUDDY_CHECK_PASSED, "%.40s", settings->home_brain_url);
        } else {
            add_check(report, "Home AI", BUDDY_CHECK_WARNING, "chosen, but no address set");
        }
        return;
    }

    if (buddy_settings_has_cloud_credentials()) {
        char description[BUDDY_CHECK_DETAIL_MAX_LEN];
        buddy_api_key_describe(settings->cloud_api_key, description, sizeof(description));

        add_check(report, "Online brain",
                  buddy_api_key_looks_plausible(settings->cloud_api_key)
                      ? BUDDY_CHECK_PASSED : BUDDY_CHECK_WARNING,
                  "%s", description);
    } else if (settings->cloud_brain_enabled) {
        /* On but unusable — that IS a fault, because it was asked for. */
        add_check(report, "Online brain", BUDDY_CHECK_WARNING, "switched on, no API key");
    } else {
        /*
         * Off is a PASS, not a note.
         *
         * The online brain is optional and costs money to run, so a box with it
         * switched off is a box working exactly as intended. Reporting that as
         * a warning made every single boot end in "1 note" and quietly trained
         * the eye to skip the notes column — which is where the real problems
         * live.
         */
        add_check(report, "Online brain", BUDDY_CHECK_PASSED, "off - answering offline");
    }
}

/* ------------------------------------------------------------------------ */
/* Public API                                                                */
/* ------------------------------------------------------------------------ */

void buddy_selftest_run(buddy_selftest_report_t *report)
{
    if (report == NULL) {
        return;
    }
    memset(report, 0, sizeof(*report));

    check_memory(report);
    check_filesystem(report);
    check_sounds(report);
    check_sensors(report);
    check_voice(report);
    check_network(report);
}

/**
 * @brief One-character marker used in the log and on screen.
 */
static const char *result_marker(buddy_check_result_t result)
{
    switch (result) {
    case BUDDY_CHECK_PASSED:  return "ok  ";
    case BUDDY_CHECK_WARNING: return "note";
    default:                  return "FAIL";
    }
}

bool buddy_selftest_run_and_log(void)
{
    buddy_selftest_report_t report;
    buddy_selftest_run(&report);

    ESP_LOGI(TAG, "---- self test ----------------------------------");
    for (uint8_t i = 0; i < report.check_count; i++) {
        const buddy_check_t *check = &report.checks[i];
        ESP_LOGI(TAG, "  [%s] %-14s %s",
                 result_marker(check->result), check->name, check->detail);
    }
    ESP_LOGI(TAG, "---- %u ok, %u notes, %u failed ------------------",
             report.passed_count, report.warning_count, report.failed_count);

    return report.failed_count == 0;
}

void buddy_selftest_format(const buddy_selftest_report_t *report,
                           char *buffer, size_t buffer_size)
{
    if (report == NULL || buffer == NULL || buffer_size == 0) {
        return;
    }
    buffer[0] = '\0';

    size_t used = 0;
    for (uint8_t i = 0; i < report->check_count; i++) {
        const buddy_check_t *check = &report->checks[i];

        const int written = snprintf(buffer + used, buffer_size - used,
                                     "%s%s: %s",
                                     (used > 0) ? "\n" : "",
                                     check->name, check->detail);
        if (written < 0 || (size_t)written >= buffer_size - used) {
            return;   /* Out of room; what is there is still readable. */
        }
        used += (size_t)written;
    }
}
