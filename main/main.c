/*
 * main.c — Bringing the desk buddy to life, in order.
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 *
 * This file is deliberately short. It does nothing except start things in the
 * right order, because the order genuinely matters:
 *
 *   1. Storage        settings and the filesystem, so everything else can read
 *                     its configuration
 *   2. Board          I2C, codec, buttons, sensors — the physical hardware
 *   3. Screen         panel and LVGL, still dark
 *   4. Face           widgets and animations, then the backlight comes on, so
 *                     the very first thing the user sees is a finished face
 *   5. Audio          speaker, chimes, playlist
 *   6. Network        Wi-Fi and the clock, in the background — never blocking
 *   7. Voice          the speech models last, because they are the slowest to
 *                     load and everything else should already be responsive
 *
 * If you are looking for behaviour rather than start-up, the interesting file
 * is voice/voice_assistant.c.
 */

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "bsp/esp-bsp.h"
#include "bsp_board.h"

#include "audio_music.h"
#include "audio_prompts.h"
#include "audio_voice.h"
#include "buddy_config.h"
#include "buddy_console.h"
#include "buddy_selftest.h"
#include "buddy_settings.h"
#include "buddy_timer.h"
#include "hw_buttons.h"
#include "hw_display.h"
#include "hw_microphone.h"
#include "hw_sensors.h"
#include "hw_speaker.h"
#include "net_time.h"
#include "net_wifi.h"
#include "ui_screen_face.h"
#include "ui_timer_badge.h"
#include "voice_assistant.h"

static const char *TAG = "main";

/**
 * @brief Prepare non-volatile storage, erasing it if it is unusable.
 */
static void initialise_storage(void)
{
    esp_err_t err = nvs_flash_init();

    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "Reformatting NVS");
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    ESP_ERROR_CHECK(buddy_settings_load());

    /* The SPIFFS image holds the chimes and the music. */
    bsp_spiffs_mount();

    /*
     * ...and, optionally, the API key.
     *
     * This has to come after the mount and after the settings load, because it
     * reads one and writes the other. It does nothing at all unless you have
     * pasted a key into spiffs/openai_key.txt — see buddy_settings.h.
     */
    buddy_settings_import_api_key_file(BUDDY_API_KEY_FILE);
}

/**
 * @brief Called when the Wi-Fi connection state changes.
 *
 * The clock is only worth asking for once there is a network to ask over.
 */
static void wifi_state_changed(net_wifi_state_t state)
{
    if (state == NET_WIFI_CONNECTED) {
        net_time_start();
    }
}

/**
 * @brief Start networking without holding up the rest of the boot.
 *
 * Everything here is asynchronous. A buddy with no Wi-Fi still works; it just
 * cannot answer open questions or tell the time.
 */
static void start_networking(void)
{
    if (net_wifi_init() != ESP_OK) {
        ESP_LOGW(TAG, "Wi-Fi did not start; running offline");
        return;
    }

    net_wifi_set_state_callback(wifi_state_changed);

    if (buddy_settings_has_wifi_credentials()) {
        net_wifi_connect_using_saved_credentials();
    } else {
        ESP_LOGI(TAG, "No Wi-Fi saved yet — set one up from the gear in the corner");
    }
}

/**
 * @brief Log how much memory is left, which is the number that matters most.
 */
static void log_memory_summary(void)
{
    ESP_LOGI(TAG, "Free memory: %u bytes internal, %u bytes PSRAM",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
}

void app_main(void)
{
    ESP_LOGI(TAG, "%s %s (built %s %s)",
             BUDDY_FIRMWARE_NAME, BUDDY_FIRMWARE_VERSION, __DATE__, __TIME__);

    /* --- 1. Storage ---------------------------------------------------- */
    initialise_storage();

    /* --- 2. Board ------------------------------------------------------ */
    ESP_ERROR_CHECK(bsp_i2c_init());

    /* --- 3. Screen (still dark) ---------------------------------------- */
    ESP_ERROR_CHECK(hw_display_init());

    /*
     * bsp_board_init() sets up the audio codec, the buttons and the sensor
     * dock. It has to come after the display because both share I2C, and the
     * display's initialisation is what brings that bus fully up.
     */
    ESP_ERROR_CHECK(bsp_board_init());

    hw_sensors_init();          /* Not fatal: a bare box simply has no readout. */
    hw_microphone_init();
    hw_buttons_init();

    /* --- 4. Face, then light --------------------------------------------- */
    ESP_ERROR_CHECK(ui_screen_face_create());

    /* A beat so the first frame is fully drawn before anyone sees it. */
    vTaskDelay(pdMS_TO_TICKS(120));
    hw_display_set_backlight_on();

    /* Say hello with a little wave of expression. */
    ui_screen_face_set_emotion(FACE_EMOTION_HAPPY);

    /* --- 5. Audio -------------------------------------------------------- */
    ESP_ERROR_CHECK(hw_speaker_init());
    audio_prompts_init();
    audio_voice_init();
    audio_music_init();

    /*
     * The countdown badge is a UI object, so it can only be wired to the timer
     * once the screen exists. Doing it here rather than inside buddy_timer.c
     * keeps that module free of any dependency on LVGL.
     */
    buddy_timer_set_tick_callback(ui_timer_badge_update);

    /* --- 6. Network (in the background) ---------------------------------- */
    start_networking();

    /* --- 7. Voice -------------------------------------------------------- */
    /*
     * Loading the speech models takes a couple of seconds and allocates a lot,
     * so it goes last, once the buddy is already blinking at the user.
     */
    ESP_LOGI(TAG, "Loading the speech models...");
    const esp_err_t voice_err = voice_assistant_start();

    if (voice_err != ESP_OK) {
        ESP_LOGE(TAG, "Voice did not start: %s", esp_err_to_name(voice_err));
        ui_screen_face_set_emotion(FACE_EMOTION_DIZZY);
        ui_screen_face_show_caption(UI_SPEECH_STATUS,
                                    "My ears did not start. Touch still works.", 0);
    } else {
        ui_screen_face_set_emotion(FACE_EMOTION_NEUTRAL);

        /*
         * A brand new box is a face that stares at you and does nothing, with
         * no clue that it is waiting to be spoken to. Say so, once, with a
         * phrase that works before any setup at all — no Wi-Fi, no API key.
         */
        if (buddy_settings_is_first_run()) {
            ui_screen_face_set_emotion(FACE_EMOTION_HAPPY);
            ui_screen_face_show_caption(
                UI_SPEECH_FROM_BUDDY,
                "Hi! Say \"" BUDDY_WAKE_WORD_ENGLISH "\", wait for the chirp, then "
                "\"show me a smile\". Tap the gear for the full list.",
                FIRST_RUN_HINT_MS);
        }
    }

    /*
      * Everything is up, so ask the buddy to check itself over. The results go
      * to the log and can be read back on screen from Settings -> Diagnostics,
      * which is far more useful than guessing why something is not working.
      */
    buddy_selftest_run_and_log();

    /*
     * Last, so every command it can run has something to talk to. Not fatal:
     * the box works the same without it, it just cannot be typed at.
     */
    buddy_console_start();

    log_memory_summary();
    ESP_LOGI(TAG, "Ready. Say \"Hi E.S.P.\" to start talking.");
}
