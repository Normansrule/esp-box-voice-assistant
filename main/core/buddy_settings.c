/*
 * buddy_settings.c — NVS-backed configuration store.
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 *
 * The whole buddy_settings_t struct is saved as a single NVS blob. That is much
 * simpler than one key per field, and because we version the layout we can
 * safely change the struct later: on a version mismatch we just fall back to
 * defaults instead of reading garbage.
 */

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

#include "buddy_api_key.h"
#include "buddy_config.h"
#include "buddy_settings.h"

static const char *TAG = "settings";

/**
 * True when this boot found nothing usable in NVS and fell back to defaults.
 *
 * Set once during buddy_settings_load() and never cleared, so the first-run
 * greeting can be shown even though saving Wi-Fi a minute later would
 * otherwise make the box look like it had been configured all along.
 */
static bool s_started_from_defaults;

/* NVS namespace and key that hold the configuration blob. */
#define SETTINGS_NVS_NAMESPACE      "buddy"
#define SETTINGS_NVS_BLOB_KEY       "config"

/*
 * Bump this whenever a field is added, removed or reordered in
 * buddy_settings_t. Saved data with a different version is discarded.
 */
/*
 * Bumped to 4 in v3.1: BUDDY_WIFI_SSID_MAX_LEN went from 32 to 33 bytes, which
 * moves every field after it. Reading a v3 blob into this struct would hand the
 * password field one byte of the SSID and shift the rest along — so the guard
 * has to reject it and fall back to defaults instead.
 *
 * Bumped to 5 in v3.2: BUDDY_CLOUD_API_KEY_MAX_LEN went from 128 to 256 for the
 * same class of reason. This time the old layout is MIGRATED rather than thrown
 * away — see migrate_from_v4() — because by now people have Wi-Fi credentials
 * and a time zone typed in on a touchscreen, and making them do all of that
 * again to fix a bug of mine is not a reasonable trade.
 *
 * Bumped to 6 in v3.5: brain_mode and requests_per_hour joined the struct in
 * the middle of it. Migrated the same way, and the migration is now the rule
 * rather than the exception — every future bump gets one.
 *
 * Bumped to 7 in v3.6: noise_profile joined them.
 *
 * Bumped to 8 after v3.7.1: home_brain_url joined the voice section, for the Home
 * AI brain mode. Migrated like the others; see migrate_from_v7().
 */
#define SETTINGS_LAYOUT_VERSION     8

/* The single in-RAM copy that every module reads from. */
static buddy_settings_t s_settings;

/* ------------------------------------------------------------------------ */
/* Migrating an older saved layout                                           */
/* ------------------------------------------------------------------------ */

/**
 * The v4 layout, frozen exactly as it was written.
 *
 * This is a copy of buddy_settings_t from firmware v3.1, kept verbatim so a
 * blob written by that firmware can be read field by field. It must never be
 * "tidied up" to match the current struct — the whole point is that it does
 * not. When a v6 layout arrives, add a v5 copy beside this one rather than
 * editing it.
 */
typedef struct {
    uint32_t settings_layout_version;

    uint8_t  speaker_volume;
    bool     chimes_enabled;

    uint8_t  screen_brightness;
    bool     idle_antics_enabled;
    bool     touch_antics_enabled;

    buddy_temperature_unit_t temperature_unit;
    bool     show_sensor_readout;

    buddy_language_t language;
    bool     cloud_brain_enabled;

    /*
     * Every size here is a literal, deliberately, where the live struct uses
     * the BUDDY_*_MAX_LEN macros. This struct describes bytes that are already
     * sitting in somebody's flash chip and can never change again; if it shared
     * the macros, editing one of them later would silently redefine the past
     * and corrupt the migration.
     */
    char     cloud_api_key[128];        /* The field that grew to 256          */
    char     cloud_base_url[128];
    char     personality_prompt[192];

    buddy_music_source_t music_source;
    char     stream_url[192];

    char     wifi_ssid[33];
    char     wifi_password[64];
    char     timezone[40];
} settings_v4_t;

/**
 * @brief Copy a v4 blob into the current struct, field by field.
 *
 * The API key is deliberately NOT carried over. Any key stored by v3.1 was
 * capped at 127 characters, so a modern project key was saved with its tail
 * missing — bringing it forward would preserve a broken value and hide the very
 * bug this version exists to fix. Better to have the box say "no API key" than
 * to have it say "key saved" and fail every request.
 */
static void migrate_from_v4(const settings_v4_t *old)
{
    buddy_settings_reset_to_defaults();

    s_settings.speaker_volume       = old->speaker_volume;
    s_settings.chimes_enabled       = old->chimes_enabled;

    s_settings.screen_brightness    = old->screen_brightness;
    s_settings.idle_antics_enabled  = old->idle_antics_enabled;
    s_settings.touch_antics_enabled = old->touch_antics_enabled;

    s_settings.temperature_unit     = old->temperature_unit;
    s_settings.show_sensor_readout  = old->show_sensor_readout;

    s_settings.language             = old->language;
    s_settings.music_source         = old->music_source;

    /*
     * A truncated key is worse than none, so the switch goes back off too. It
     * turns itself on again the moment a full key is entered or flashed.
     */
    s_settings.cloud_brain_enabled  = false;
    s_settings.cloud_api_key[0]     = '\0';

    /* Everything the user typed by hand, which is the point of migrating. */
    strncpy(s_settings.cloud_base_url, old->cloud_base_url,
            sizeof(s_settings.cloud_base_url) - 1);
    strncpy(s_settings.personality_prompt, old->personality_prompt,
            sizeof(s_settings.personality_prompt) - 1);
    strncpy(s_settings.stream_url, old->stream_url, sizeof(s_settings.stream_url) - 1);
    strncpy(s_settings.wifi_ssid, old->wifi_ssid, sizeof(s_settings.wifi_ssid) - 1);
    strncpy(s_settings.wifi_password, old->wifi_password, sizeof(s_settings.wifi_password) - 1);
    strncpy(s_settings.timezone, old->timezone, sizeof(s_settings.timezone) - 1);

    s_settings.settings_layout_version = SETTINGS_LAYOUT_VERSION;

    ESP_LOGW(TAG, "Upgraded saved configuration from layout v4 to v%d", SETTINGS_LAYOUT_VERSION);
    ESP_LOGW(TAG, "Wi-Fi (%s) kept; the API key was cleared because the old one "
                  "could only hold 127 of its ~165 characters",
             s_settings.wifi_ssid[0] ? s_settings.wifi_ssid : "none saved");
}

/**
 * The v5 layout, frozen exactly as v3.2 to v3.4 wrote it.
 *
 * Same rules as settings_v4_t above: literal sizes, never tidied to match the
 * live struct, and a new copy is added rather than this one edited.
 */
typedef struct {
    uint32_t settings_layout_version;

    uint8_t  speaker_volume;
    bool     chimes_enabled;

    uint8_t  screen_brightness;
    bool     idle_antics_enabled;
    bool     touch_antics_enabled;

    buddy_temperature_unit_t temperature_unit;
    bool     show_sensor_readout;

    buddy_language_t language;
    bool     cloud_brain_enabled;
    /* brain_mode and requests_per_hour arrive after this point in v6. */
    char     cloud_api_key[256];
    char     cloud_base_url[128];
    char     personality_prompt[192];

    buddy_music_source_t music_source;
    char     stream_url[192];

    char     wifi_ssid[33];
    char     wifi_password[64];
    char     timezone[40];
} settings_v5_t;

/**
 * @brief Copy a v5 blob into the current struct, field by field.
 *
 * The API key DOES come across this time, unlike the v4 migration. A v5 key was
 * stored in a 256-byte field and so is whole; the v4 one was cut short at 127
 * characters and was worth destroying.
 */
static void migrate_from_v5(const settings_v5_t *old)
{
    buddy_settings_reset_to_defaults();

    s_settings.speaker_volume       = old->speaker_volume;
    s_settings.chimes_enabled       = old->chimes_enabled;

    s_settings.screen_brightness    = old->screen_brightness;
    s_settings.idle_antics_enabled  = old->idle_antics_enabled;
    s_settings.touch_antics_enabled = old->touch_antics_enabled;

    s_settings.temperature_unit     = old->temperature_unit;
    s_settings.show_sensor_readout  = old->show_sensor_readout;

    s_settings.language             = old->language;
    s_settings.cloud_brain_enabled  = old->cloud_brain_enabled;
    s_settings.music_source         = old->music_source;

    memcpy(s_settings.cloud_api_key, old->cloud_api_key, sizeof(old->cloud_api_key));
    s_settings.cloud_api_key[sizeof(s_settings.cloud_api_key) - 1] = '\0';

    strncpy(s_settings.cloud_base_url, old->cloud_base_url,
            sizeof(s_settings.cloud_base_url) - 1);
    strncpy(s_settings.personality_prompt, old->personality_prompt,
            sizeof(s_settings.personality_prompt) - 1);
    strncpy(s_settings.stream_url, old->stream_url, sizeof(s_settings.stream_url) - 1);
    strncpy(s_settings.wifi_ssid, old->wifi_ssid, sizeof(s_settings.wifi_ssid) - 1);
    strncpy(s_settings.wifi_password, old->wifi_password, sizeof(s_settings.wifi_password) - 1);
    strncpy(s_settings.timezone, old->timezone, sizeof(s_settings.timezone) - 1);

    /*
     * The two new fields take their defaults, which is the cautious end of
     * both: Smart rather than Always ask, and an hourly ceiling rather than
     * none. An upgrade should never make a box spend more freely than it did
     * before the upgrade.
     */
    s_settings.brain_mode        = BUDDY_BRAIN_SMART;
    s_settings.requests_per_hour = BUDDY_DEFAULT_REQUESTS_PER_HOUR;

    s_settings.settings_layout_version = SETTINGS_LAYOUT_VERSION;

    ESP_LOGW(TAG, "Upgraded saved configuration from layout v5 to v%d", SETTINGS_LAYOUT_VERSION);
    ESP_LOGI(TAG, "Online brain stays %s, now in Smart mode with %d questions per hour",
             s_settings.cloud_brain_enabled ? "on" : "off", BUDDY_DEFAULT_REQUESTS_PER_HOUR);
}

/**
 * The v6 layout, frozen exactly as v3.5 wrote it. Same rules as the two above.
 */
typedef struct {
    uint32_t settings_layout_version;

    uint8_t  speaker_volume;
    bool     chimes_enabled;

    uint8_t  screen_brightness;
    bool     idle_antics_enabled;
    bool     touch_antics_enabled;

    buddy_temperature_unit_t temperature_unit;
    bool     show_sensor_readout;

    buddy_language_t language;
    bool     cloud_brain_enabled;
    buddy_brain_mode_t brain_mode;
    uint16_t requests_per_hour;
    /* noise_profile arrives after this point in v7. */
    char     cloud_api_key[256];
    char     cloud_base_url[128];
    char     personality_prompt[192];

    buddy_music_source_t music_source;
    char     stream_url[192];

    char     wifi_ssid[33];
    char     wifi_password[64];
    char     timezone[40];
} settings_v6_t;

/**
 * @brief Copy a v6 blob into the current struct, field by field.
 */
static void migrate_from_v6(const settings_v6_t *old)
{
    buddy_settings_reset_to_defaults();

    s_settings.speaker_volume       = old->speaker_volume;
    s_settings.chimes_enabled       = old->chimes_enabled;

    s_settings.screen_brightness    = old->screen_brightness;
    s_settings.idle_antics_enabled  = old->idle_antics_enabled;
    s_settings.touch_antics_enabled = old->touch_antics_enabled;

    s_settings.temperature_unit     = old->temperature_unit;
    s_settings.show_sensor_readout  = old->show_sensor_readout;

    s_settings.language             = old->language;
    s_settings.cloud_brain_enabled  = old->cloud_brain_enabled;
    s_settings.brain_mode           = old->brain_mode;
    s_settings.requests_per_hour    = old->requests_per_hour;
    s_settings.music_source         = old->music_source;

    memcpy(s_settings.cloud_api_key, old->cloud_api_key, sizeof(old->cloud_api_key));
    s_settings.cloud_api_key[sizeof(s_settings.cloud_api_key) - 1] = '\0';

    strncpy(s_settings.cloud_base_url, old->cloud_base_url,
            sizeof(s_settings.cloud_base_url) - 1);
    strncpy(s_settings.personality_prompt, old->personality_prompt,
            sizeof(s_settings.personality_prompt) - 1);
    strncpy(s_settings.stream_url, old->stream_url, sizeof(s_settings.stream_url) - 1);
    strncpy(s_settings.wifi_ssid, old->wifi_ssid, sizeof(s_settings.wifi_ssid) - 1);
    strncpy(s_settings.wifi_password, old->wifi_password, sizeof(s_settings.wifi_password) - 1);
    strncpy(s_settings.timezone, old->timezone, sizeof(s_settings.timezone) - 1);

    /* The new field takes its default, which is "work it out yourself". */
    s_settings.noise_profile = BUDDY_NOISE_AUTOMATIC;

    s_settings.settings_layout_version = SETTINGS_LAYOUT_VERSION;

    ESP_LOGW(TAG, "Upgraded saved configuration from layout v6 to v%d", SETTINGS_LAYOUT_VERSION);
}

/**
 * The v7 layout, frozen exactly as v3.6 to v3.7 wrote it. Same rules as the
 * three above.
 */
typedef struct {
    uint32_t settings_layout_version;

    uint8_t  speaker_volume;
    bool     chimes_enabled;

    uint8_t  screen_brightness;
    bool     idle_antics_enabled;
    bool     touch_antics_enabled;

    buddy_temperature_unit_t temperature_unit;
    bool     show_sensor_readout;

    buddy_language_t language;
    bool     cloud_brain_enabled;
    buddy_brain_mode_t brain_mode;
    uint16_t requests_per_hour;
    buddy_noise_profile_t noise_profile;
    char     cloud_api_key[256];
    char     cloud_base_url[128];
    char     personality_prompt[192];
    /* home_brain_url arrives after this point in v8. */

    buddy_music_source_t music_source;
    char     stream_url[192];

    char     wifi_ssid[33];
    char     wifi_password[64];
    char     timezone[40];
} settings_v7_t;

/**
 * @brief Copy a v7 blob into the current struct, field by field.
 *
 * Every v7 field comes across unchanged. The new one, home_brain_url, starts
 * empty: the home brain is something you set up on purpose, never something an
 * upgrade switches on.
 */
static void migrate_from_v7(const settings_v7_t *old)
{
    buddy_settings_reset_to_defaults();

    s_settings.speaker_volume       = old->speaker_volume;
    s_settings.chimes_enabled       = old->chimes_enabled;

    s_settings.screen_brightness    = old->screen_brightness;
    s_settings.idle_antics_enabled  = old->idle_antics_enabled;
    s_settings.touch_antics_enabled = old->touch_antics_enabled;

    s_settings.temperature_unit     = old->temperature_unit;
    s_settings.show_sensor_readout  = old->show_sensor_readout;

    s_settings.language             = old->language;
    s_settings.cloud_brain_enabled  = old->cloud_brain_enabled;
    s_settings.brain_mode           = old->brain_mode;
    s_settings.requests_per_hour    = old->requests_per_hour;
    s_settings.noise_profile        = old->noise_profile;
    s_settings.music_source         = old->music_source;

    memcpy(s_settings.cloud_api_key, old->cloud_api_key, sizeof(old->cloud_api_key));
    s_settings.cloud_api_key[sizeof(s_settings.cloud_api_key) - 1] = '\0';

    strncpy(s_settings.cloud_base_url, old->cloud_base_url,
            sizeof(s_settings.cloud_base_url) - 1);
    strncpy(s_settings.personality_prompt, old->personality_prompt,
            sizeof(s_settings.personality_prompt) - 1);
    strncpy(s_settings.stream_url, old->stream_url, sizeof(s_settings.stream_url) - 1);
    strncpy(s_settings.wifi_ssid, old->wifi_ssid, sizeof(s_settings.wifi_ssid) - 1);
    strncpy(s_settings.wifi_password, old->wifi_password, sizeof(s_settings.wifi_password) - 1);
    strncpy(s_settings.timezone, old->timezone, sizeof(s_settings.timezone) - 1);

    s_settings.home_brain_url[0] = '\0';

    s_settings.settings_layout_version = SETTINGS_LAYOUT_VERSION;

    ESP_LOGW(TAG, "Upgraded saved configuration from layout v7 to v%d", SETTINGS_LAYOUT_VERSION);
}

/**
 * @brief Try to read a blob written by an older firmware and bring it forward.
 *
 * Newest layout first: the sizes differ, so trying v5 before v4 means the wrong
 * one is rejected on size alone rather than on a version field that happens to
 * land in a plausible place.
 *
 * @return true when an older layout was recognised and migrated.
 */
static bool try_migrate_older_layout(nvs_handle_t nvs)
{
    /*
     * STATIC, not on the stack, and this is not a micro-optimisation.
     *
     * These two are 940 and 812 bytes. The caller already has a 948-byte
     * buddy_settings_t in its own frame, and all of it runs on app_main, whose
     * stack is 3584 bytes by default. That is 2700 bytes of structs with under
     * 900 left for nvs_get_blob and everything beneath it — which overflows,
     * and overflows at the FIRST thing app_main does, before the display is
     * even initialised. The whole failure presents as a black screen with no
     * clue as to why.
     *
     * v3.4 got away with one frozen layout here. Adding the second in v3.5 is
     * what pushed it over, and adding a third would have done it again. Static
     * is the shape that does not have that failure mode: the cost is 1752
     * bytes of .bss that nothing else wanted, paid once.
     *
     * Safe as statics because this runs exactly once, from app_main, before any
     * other task exists.
     */
    static settings_v7_t seven;
    static settings_v6_t six;
    static settings_v5_t five;
    static settings_v4_t four;

    size_t blob_size = sizeof(seven);

    if (nvs_get_blob(nvs, SETTINGS_NVS_BLOB_KEY, &seven, &blob_size) == ESP_OK &&
        blob_size == sizeof(seven) && seven.settings_layout_version == 7) {
        migrate_from_v7(&seven);
        return true;
    }

    blob_size = sizeof(six);

    if (nvs_get_blob(nvs, SETTINGS_NVS_BLOB_KEY, &six, &blob_size) == ESP_OK &&
        blob_size == sizeof(six) && six.settings_layout_version == 6) {
        migrate_from_v6(&six);
        return true;
    }

    blob_size = sizeof(five);

    if (nvs_get_blob(nvs, SETTINGS_NVS_BLOB_KEY, &five, &blob_size) == ESP_OK &&
        blob_size == sizeof(five) && five.settings_layout_version == 5) {
        migrate_from_v5(&five);
        return true;
    }

    blob_size = sizeof(four);

    if (nvs_get_blob(nvs, SETTINGS_NVS_BLOB_KEY, &four, &blob_size) == ESP_OK &&
        blob_size == sizeof(four) && four.settings_layout_version == 4) {
        migrate_from_v4(&four);
        return true;
    }

    return false;
}

/* ------------------------------------------------------------------------ */
/* Defaults                                                                  */
/* ------------------------------------------------------------------------ */

void buddy_settings_reset_to_defaults(void)
{
    memset(&s_settings, 0, sizeof(s_settings));

    s_settings.settings_layout_version = SETTINGS_LAYOUT_VERSION;

    /* Sound: loud enough to hear across a desk, chimes on. */
    s_settings.speaker_volume     = 70;
    s_settings.chimes_enabled     = true;

    /* Screen: bright, lively, and responsive to touch. */
    s_settings.screen_brightness  = 90;
    s_settings.idle_antics_enabled  = true;
    s_settings.touch_antics_enabled = true;

    /* Sensors. */
    s_settings.temperature_unit   = BUDDY_TEMPERATURE_FAHRENHEIT;
    s_settings.show_sensor_readout = true;

    /*
     * Voice: English wake word, and the online brain off.
     *
     * Smart is the mode it will be in IF somebody turns it on, which is the
     * useful default for that moment — but nothing reaches the network while
     * cloud_brain_enabled is false, whatever the mode says.
     */
    s_settings.language            = BUDDY_LANGUAGE_ENGLISH;
    s_settings.cloud_brain_enabled = false;
    s_settings.brain_mode          = BUDDY_BRAIN_SMART;
    s_settings.requests_per_hour   = BUDDY_DEFAULT_REQUESTS_PER_HOUR;

    /* Listening: let the box work the room out for itself. */
    s_settings.noise_profile       = BUDDY_NOISE_AUTOMATIC;
    strncpy(s_settings.cloud_base_url, BUDDY_DEFAULT_CLOUD_BASE_URL,
            sizeof(s_settings.cloud_base_url) - 1);
    strncpy(s_settings.personality_prompt,
            "You are a small, cheerful robot buddy living on a desk. "
            "Answer in at most two short sentences. Be warm and a little playful.",
            sizeof(s_settings.personality_prompt) - 1);

    /* Music: play whatever was flashed into the storage partition. */
    s_settings.music_source       = BUDDY_MUSIC_SOURCE_FLASH;

    /* Clock: US Pacific, with the usual daylight-saving rules. */
    strncpy(s_settings.timezone, "PST8PDT,M3.2.0,M11.1.0",
            sizeof(s_settings.timezone) - 1);

    ESP_LOGI(TAG, "Applied factory defaults (layout v%d)", SETTINGS_LAYOUT_VERSION);
}

/* ------------------------------------------------------------------------ */
/* Load / save                                                               */
/* ------------------------------------------------------------------------ */

esp_err_t buddy_settings_load(void)
{
    nvs_handle_t nvs;

    esp_err_t err = nvs_open(SETTINGS_NVS_NAMESPACE, NVS_READONLY, &nvs);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "No saved configuration (%s); using defaults", esp_err_to_name(err));
        buddy_settings_reset_to_defaults();
        s_started_from_defaults = true;
        return ESP_OK;
    }

    /* Static for the same reason as the migration structs below — see there. */
    static buddy_settings_t loaded;

    size_t blob_size = sizeof(loaded);
    err = nvs_get_blob(nvs, SETTINGS_NVS_BLOB_KEY, &loaded, &blob_size);

    bool usable = (err == ESP_OK) &&
                  (blob_size == sizeof(loaded)) &&
                  (loaded.settings_layout_version == SETTINGS_LAYOUT_VERSION);

    if (!usable) {
        /*
         * Not this layout. Before falling back to factory defaults — which
         * throws away Wi-Fi credentials somebody typed a character at a time on
         * a touchscreen — see whether it is a layout we know how to bring
         * forward.
         */
        const bool migrated = try_migrate_older_layout(nvs);
        nvs_close(nvs);

        if (!migrated) {
            ESP_LOGW(TAG, "Saved configuration unusable (err=%s, size=%u, ver=%u); using defaults",
                     esp_err_to_name(err), (unsigned)blob_size,
                     (err == ESP_OK) ? (unsigned)loaded.settings_layout_version : 0u);
            buddy_settings_reset_to_defaults();
            s_started_from_defaults = true;
            return ESP_OK;
        }

        /*
         * Write the upgraded layout back straight away, so the migration runs
         * exactly once rather than on every boot from here on.
         */
        buddy_settings_save();
        return ESP_OK;
    }

    nvs_close(nvs);
    s_settings = loaded;

    /*
     * Guard against nonsense that could brick the UI (for example a zero
     * brightness that makes the screen look dead).
     */
    if (s_settings.speaker_volume > 100) {
        s_settings.speaker_volume = 70;
    }
    if (s_settings.screen_brightness < 10 || s_settings.screen_brightness > 100) {
        s_settings.screen_brightness = 90;
    }
    if (s_settings.language >= BUDDY_LANGUAGE_COUNT) {
        s_settings.language = BUDDY_LANGUAGE_ENGLISH;
    }
    if (s_settings.cloud_base_url[0] == '\0') {
        strncpy(s_settings.cloud_base_url, BUDDY_DEFAULT_CLOUD_BASE_URL,
                sizeof(s_settings.cloud_base_url) - 1);
    }
    if (s_settings.brain_mode >= BUDDY_BRAIN_MODE_COUNT) {
        s_settings.brain_mode = BUDDY_BRAIN_SMART;
    }
    if (s_settings.noise_profile >= BUDDY_NOISE_PROFILE_COUNT) {
        s_settings.noise_profile = BUDDY_NOISE_AUTOMATIC;
    }

    ESP_LOGI(TAG, "Configuration loaded (volume=%u, brightness=%u, cloud=%s, wifi=%s)",
             s_settings.speaker_volume, s_settings.screen_brightness,
             s_settings.cloud_brain_enabled ? "on" : "off",
             s_settings.wifi_ssid[0] ? s_settings.wifi_ssid : "(none)");
    return ESP_OK;
}

esp_err_t buddy_settings_save(void)
{
    nvs_handle_t nvs;

    esp_err_t err = nvs_open(SETTINGS_NVS_NAMESPACE, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Cannot open NVS for writing: %s", esp_err_to_name(err));
        return err;
    }

    s_settings.settings_layout_version = SETTINGS_LAYOUT_VERSION;

    err = nvs_set_blob(nvs, SETTINGS_NVS_BLOB_KEY, &s_settings, sizeof(s_settings));
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }
    nvs_close(nvs);

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Configuration saved");
    } else {
        ESP_LOGE(TAG, "Save failed: %s", esp_err_to_name(err));
    }
    return err;
}

/* ------------------------------------------------------------------------ */
/* Accessors                                                                 */
/* ------------------------------------------------------------------------ */

buddy_settings_t *buddy_settings_get(void)
{
    return &s_settings;
}

bool buddy_settings_is_first_run(void)
{
    return s_started_from_defaults;
}

bool buddy_settings_has_wifi_credentials(void)
{
    return s_settings.wifi_ssid[0] != '\0';
}

bool buddy_settings_has_cloud_credentials(void)
{
    return s_settings.cloud_brain_enabled && (s_settings.cloud_api_key[0] != '\0');
}

bool buddy_settings_has_home_brain(void)
{
    return s_settings.home_brain_url[0] != '\0';
}

/* ------------------------------------------------------------------------ */
/* The API key file                                                          */
/* ------------------------------------------------------------------------ */

esp_err_t buddy_settings_import_api_key_file(const char *path)
{
    if (path == NULL || path[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }

    /*
     * fopen, not access(). SPIFFS registers no access() handler with the
     * virtual filesystem layer, so access() on this filesystem ALWAYS fails
     * with ENOSYS regardless of whether the file is there. That mistake once
     * cost this project an evening of "the chimes are missing" when the chimes
     * were sitting on flash the whole time. Opening the file is the only
     * reliable way to ask.
     */
    FILE *file = fopen(path, "r");
    if (file == NULL) {
        ESP_LOGD(TAG, "No API key file at %s", path);
        return ESP_ERR_NOT_FOUND;
    }

    /*
     * Room for the longest key we can store plus a generous margin, so an
     * oversized key is READ in full and then rejected by the sanitiser. Reading
     * only as much as we can store would truncate it here instead, which is the
     * exact failure this file exists to prevent.
     */
    char raw[BUDDY_CLOUD_API_KEY_MAX_LEN + 128];
    const size_t read_bytes = fread(raw, 1, sizeof(raw) - 1, file);
    fclose(file);

    raw[read_bytes] = '\0';

    char key[BUDDY_CLOUD_API_KEY_MAX_LEN];
    const size_t length = buddy_api_key_sanitise(raw, key, sizeof(key));

    if (length == 0) {
        ESP_LOGW(TAG, "%s holds no usable key (%u bytes read)", path, (unsigned)read_bytes);
        ESP_LOGW(TAG, "Paste the whole key on the FIRST line, starting \"sk-\", "
                      "with no line break inside it");
        return ESP_ERR_INVALID_SIZE;
    }

    /*
     * Too short to be a key at all.
     *
     * This is what the file ships as — a placeholder line telling you to paste
     * your key over it — and it is also what you get from a stray word, a URL,
     * or a key that got line-wrapped by an editor. Adopting any of those would
     * switch the online brain on with nonsense in it and, worse, overwrite a
     * perfectly good key somebody typed on the screen. So the floor is a hard
     * refusal rather than a warning.
     *
     * The prefix, by contrast, is only warned about: the base URL can be
     * pointed at an OpenAI-compatible proxy, and those issue keys in their own
     * formats.
     */
    if (length < BUDDY_API_KEY_MIN_PLAUSIBLE_LEN) {
        ESP_LOGI(TAG, "%s still holds the placeholder, not a key — ignoring it", path);
        return ESP_ERR_INVALID_SIZE;
    }

    if (strcmp(key, s_settings.cloud_api_key) == 0) {
        ESP_LOGI(TAG, "API key file matches the saved key; nothing to do");
        return ESP_ERR_INVALID_STATE;
    }

    char description[48];
    buddy_api_key_describe(key, description, sizeof(description));
    ESP_LOGI(TAG, "Adopted the API key from %s (%s)", path, description);

    if (!buddy_api_key_looks_plausible(key)) {
        ESP_LOGW(TAG, "That does not start with \"sk-\". Using it anyway, in case "
                      "you are pointing at a proxy.");
    }

    memcpy(s_settings.cloud_api_key, key, length + 1);

    /*
     * The key is stored, and the switch stays exactly where the user left it.
     *
     * This deliberately does NOT turn the online brain on. Sending questions to
     * OpenAI costs money, and a firmware update quietly starting to spend it
     * because a file happened to be in the build is not a decision this code
     * gets to make. Everything worth doing on this box — the phrases, the
     * faces, the timer, the sensors, the music — works with it off.
     *
     * Settings -> ONLINE BRAIN -> "Answer any question" is the one switch.
     */
    if (!s_settings.cloud_brain_enabled) {
        ESP_LOGI(TAG, "Key stored, online brain left OFF. Turn it on in Settings "
                      "when you want it — it bills to your OpenAI account.");
    }

    return buddy_settings_save();
}
