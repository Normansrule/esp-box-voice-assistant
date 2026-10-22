/*
 * buddy_settings.h — Everything the user can configure, stored in NVS flash.
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 *
 * One struct holds the whole configuration. It is read once at boot into RAM,
 * handed out by pointer, and written back to flash only when something asks for
 * it. That keeps flash writes rare (they are slow and wear the chip out) while
 * letting any module read a setting for free.
 *
 * Typical use:
 *      buddy_settings_t *cfg = buddy_settings_get();
 *      cfg->speaker_volume = 70;
 *      buddy_settings_save();
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Which language the wake word and command recogniser are loaded for. */
typedef enum {
    BUDDY_LANGUAGE_ENGLISH = 0,   /*!< Wake word "Hi E.S.P.", English commands  */
    BUDDY_LANGUAGE_CHINESE,       /*!< Wake word "Hi Lexin", Chinese commands   */
    BUDDY_LANGUAGE_COUNT,
} buddy_language_t;

/** Unit used when the temperature is displayed or spoken. */
typedef enum {
    BUDDY_TEMPERATURE_CELSIUS = 0,
    BUDDY_TEMPERATURE_FAHRENHEIT,
} buddy_temperature_unit_t;

/**
 * @brief How hard the buddy should try before it reaches for the internet.
 *
 * The single most consequential setting on the box, because it is the only one
 * that can cost money. See voice_router.h for exactly what each mode does with
 * a question it did not recognise.
 */
typedef enum {
    /** Never ask anyone. A complete product on its own, and the default. */
    BUDDY_BRAIN_OFFLINE_ONLY = 0,

    /**
     * Ask only when the question is genuinely beyond the chip AND somebody
     * genuinely asked it AND the hourly allowance has room. The mode worth
     * using if you want both an assistant and a predictable bill.
     */
    BUDDY_BRAIN_SMART,

    /** Anything unrecognised goes up. Fastest to answer, quickest to spend. */
    BUDDY_BRAIN_ALWAYS_ASK,

    BUDDY_BRAIN_MODE_COUNT,
} buddy_brain_mode_t;

/**
 * @brief How much racket the box should expect to be spoken to over.
 *
 * The buddy measures its own room continuously and adapts, so AUTOMATIC is
 * right almost everywhere. The other two exist because "almost everywhere" is
 * not everywhere: a room with a steady loud source the box learns as normal
 * needs a bigger margin, and a very quiet room where somebody speaks softly
 * needs a smaller one.
 */
typedef enum {
    BUDDY_NOISE_AUTOMATIC = 0,  /*!< Adapt to whatever the room turns out to be */
    BUDDY_NOISE_QUIET_ROOM,     /*!< Listen harder; a study, a bedroom          */
    BUDDY_NOISE_NOISY_ROOM,     /*!< Demand more; a kitchen, a workshop         */
    BUDDY_NOISE_PROFILE_COUNT,
} buddy_noise_profile_t;

/** Where music comes from when the user asks the buddy to play something. */
typedef enum {
    BUDDY_MUSIC_SOURCE_FLASH = 0, /*!< .mp3 files baked into the SPIFFS image  */
    BUDDY_MUSIC_SOURCE_STREAM,    /*!< .mp3 URLs fetched over Wi-Fi            */
} buddy_music_source_t;

/** Maximum lengths for the text fields the user can type on the touchscreen. */
/*
 * 33, not 32. An SSID may be a full 32 BYTES, and strncpy(dst, src, size - 1)
 * into a 32-byte buffer silently drops the last character of one — which fails
 * to connect, with a "network not found" that looks like the radio's fault.
 */
#define BUDDY_WIFI_SSID_MAX_LEN         33
#define BUDDY_WIFI_PASSWORD_MAX_LEN     64
/*
 * 256, not 128.
 *
 * This one was a real bug, and an invisible one. OpenAI's original keys were
 * "sk-" followed by 48 characters — 51 in total, which fits in 128 with room to
 * spare. Project keys replaced them: "sk-proj-" followed by a project reference
 * and the secret, around 165 characters, and service-account keys
 * ("sk-svcacct-") are longer still.
 *
 * At 128 the buffer holds 127 characters, and the text entry field was capped
 * at the same figure. So the user typed a 165-character key, the keyboard
 * quietly stopped accepting input four fifths of the way through, and the box
 * saved a key that was missing its last forty characters. Every request then
 * came back 401 with nothing on screen to explain it.
 *
 * 256 covers every key format OpenAI currently issues with room for the next
 * one. It costs 128 bytes of NVS, which is nothing.
 */
#define BUDDY_CLOUD_API_KEY_MAX_LEN     256
#define BUDDY_CLOUD_BASE_URL_MAX_LEN    128
#define BUDDY_STREAM_URL_MAX_LEN        192
#define BUDDY_TIMEZONE_MAX_LEN          40
#define BUDDY_PERSONALITY_MAX_LEN       192

/**
 * @brief The complete, persisted configuration of the buddy.
 *
 * @note Adding a field is safe: `settings_layout_version` is bumped, old data is
 *       discarded, and defaults are applied. Never reorder existing fields
 *       without bumping the version.
 */
typedef struct {
    uint32_t settings_layout_version;   /*!< Guards against reading stale layouts */

    /* --- Sound ------------------------------------------------------------ */
    uint8_t  speaker_volume;            /*!< 0-100, applied to music and speech   */
    bool     chimes_enabled;            /*!< Play the little "ding" on wake-up    */

    /* --- Screen ----------------------------------------------------------- */
    uint8_t  screen_brightness;         /*!< 10-100 percent backlight             */
    bool     idle_antics_enabled;       /*!< Random winks/looks while idle        */
    bool     touch_antics_enabled;      /*!< React with an emote when touched     */

    /* --- Sensors ---------------------------------------------------------- */
    buddy_temperature_unit_t temperature_unit;
    bool     show_sensor_readout;       /*!< Draw the top-right temp/humidity chip */

    /* --- Voice ------------------------------------------------------------ */
    buddy_language_t language;

    /**
     * Kept alongside brain_mode rather than replaced by it.
     *
     * The switch answers "is the online brain allowed at all", the mode answers
     * "how eagerly". Two questions, and collapsing them would mean losing the
     * ability to turn the whole thing off without also forgetting which mode
     * you liked. Nothing reaches the network unless BOTH say so.
     */
    bool     cloud_brain_enabled;       /*!< Send unmatched questions to the cloud */
    buddy_brain_mode_t brain_mode;      /*!< How readily to use it                 */
    uint16_t requests_per_hour;         /*!< Ceiling on cloud requests; 0 = none    */
    buddy_noise_profile_t noise_profile;/*!< How much room noise to expect          */

    char     cloud_api_key[BUDDY_CLOUD_API_KEY_MAX_LEN];
    char     cloud_base_url[BUDDY_CLOUD_BASE_URL_MAX_LEN];
    char     personality_prompt[BUDDY_PERSONALITY_MAX_LEN];

    /* --- Music ------------------------------------------------------------ */
    buddy_music_source_t music_source;
    char     stream_url[BUDDY_STREAM_URL_MAX_LEN];

    /* --- Network ---------------------------------------------------------- */
    char     wifi_ssid[BUDDY_WIFI_SSID_MAX_LEN];
    char     wifi_password[BUDDY_WIFI_PASSWORD_MAX_LEN];
    char     timezone[BUDDY_TIMEZONE_MAX_LEN];  /*!< POSIX TZ string, e.g. PST8PDT,M3.2.0,M11.1.0 */
} buddy_settings_t;

/**
 * @brief Load the configuration from NVS, or create it from defaults.
 *
 * Call this once, early in app_main(), after nvs_flash_init().
 *
 * @return ESP_OK on success (including the "first boot, used defaults" case).
 */
esp_err_t buddy_settings_load(void);

/**
 * @brief Get a pointer to the live, in-RAM configuration.
 *
 * The pointer stays valid for the lifetime of the program. Modify fields
 * directly, then call buddy_settings_save() to make the change survive a reboot.
 */
buddy_settings_t *buddy_settings_get(void);

/**
 * @brief Write the in-RAM configuration back to NVS flash.
 */
esp_err_t buddy_settings_save(void);

/**
 * @brief Overwrite the in-RAM configuration with factory defaults.
 *
 * Does not touch flash; follow it with buddy_settings_save() to make it stick.
 */
void buddy_settings_reset_to_defaults(void);

/**
 * @brief True when a Wi-Fi network has been configured by the user.
 */
/**
 * @brief True when this boot found no saved configuration at all.
 *
 * Stays true for the whole session even after settings are saved, so the
 * first-run greeting is a property of the boot rather than of the current
 * contents of NVS.
 */
bool buddy_settings_is_first_run(void);

bool buddy_settings_has_wifi_credentials(void);

/**
 * @brief True when the cloud brain is switched on AND has an API key.
 */
bool buddy_settings_has_cloud_credentials(void);

/**
 * @brief Adopt an API key from a file on the flashed filesystem.
 *
 * Typing a 165-character key on a 320x240 touchscreen is a genuinely miserable
 * way to spend five minutes, and one mistyped character is indistinguishable
 * from a wrong key. So the key may instead be pasted into a text file that gets
 * baked into the storage partition at build time.
 *
 * WHICH KEY WINS
 * --------------
 * The file does, whenever it holds something different from what is saved.
 * Shipping a key in the build is a deliberate act — you edited a file and
 * reflashed the box — so it behaves like the rest of the build: what you
 * flashed is what runs. Rotating a key is then "edit the file, reflash", with
 * no hunt through the settings screens.
 *
 * To manage the key from the touchscreen instead, simply do not ship the file.
 * An absent or empty file changes nothing.
 *
 * Call this once at boot, after the filesystem is mounted.
 *
 * @param path  File to read, e.g. "/spiffs/openai_key.txt".
 *
 * @return ESP_OK if a key was adopted and saved, ESP_ERR_NOT_FOUND if there is
 *         no such file, ESP_ERR_INVALID_STATE if the file matched what was
 *         already saved, ESP_ERR_INVALID_SIZE if it held no usable key.
 */
esp_err_t buddy_settings_import_api_key_file(const char *path);

#ifdef __cplusplus
}
#endif
