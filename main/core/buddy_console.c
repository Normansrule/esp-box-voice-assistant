/*
 * buddy_console.c — Talking to the buddy over its USB cable.
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 *
 * See buddy_console.h for the protocol. This file is a line reader, a table of
 * commands, and a JSON writer small enough to read in one sitting — the
 * esp_console component would bring linenoise with it, and linenoise probes
 * the terminal with escape codes that a web page is not going to answer.
 */

#include "buddy_console.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"

#include "buddy_config.h"
#include "buddy_selftest.h"
#include "buddy_settings.h"
#include "buddy_timer.h"
#include "face_emotions.h"
#include "hw_display.h"
#include "hw_sensors.h"
#include "hw_speaker.h"
#include "net_wifi.h"
#include "ui_screen_face.h"
#include "voice_assistant.h"
#include "voice_commands.h"

#if CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"
#endif

static const char *TAG = "console";

/** Longest command line accepted; anything longer is discarded whole. */
#define CONSOLE_LINE_MAX_LEN    160

/* ------------------------------------------------------------------------ */
/* Writing answers                                                           */
/* ------------------------------------------------------------------------ */

/**
 * @brief Print a string as a JSON string literal, quotes included.
 */
static void print_json_string(const char *text)
{
    fputs("\"", stdout);
    for (const char *c = (text != NULL) ? text : ""; *c != '\0'; c++) {
        switch (*c) {
        case '\x22': fputs("\\\"", stdout); break;  /* a double quote */
        case '\\':   fputs("\\\\", stdout); break;
        case '\n': fputs("\\n", stdout);  break;
        case '\r': fputs("\\r", stdout);  break;
        case '\t': fputs("\\t", stdout);  break;
        default:
            if ((unsigned char)*c < 0x20) {
                printf("\\u%04x", (unsigned)(unsigned char)*c);
            } else {
                putchar(*c);
            }
        }
    }
    fputs("\"", stdout);
}

static void begin_reply(const char *command, bool ok)
{
    printf(BUDDY_CONSOLE_REPLY_PREFIX "{\"cmd\":");
    print_json_string(command);
    printf(",\"ok\":%s", ok ? "true" : "false");
}

static void end_reply(void)
{
    puts("}");
    fflush(stdout);
}

static void reply_error(const char *command, const char *error)
{
    begin_reply(command, false);
    printf(",\"error\":");
    print_json_string(error);
    end_reply();
}

/* ------------------------------------------------------------------------ */
/* The commands                                                              */
/* ------------------------------------------------------------------------ */

static const char *wifi_state_name(net_wifi_state_t state)
{
    switch (state) {
    case NET_WIFI_IDLE:       return "idle";
    case NET_WIFI_CONNECTING: return "connecting";
    case NET_WIFI_CONNECTED:  return "connected";
    case NET_WIFI_FAILED:     return "failed";
    default:                  return "unknown";
    }
}

static const char *brain_mode_name(buddy_brain_mode_t mode)
{
    switch (mode) {
    case BUDDY_BRAIN_OFFLINE_ONLY: return "offline";
    case BUDDY_BRAIN_SMART:        return "smart";
    case BUDDY_BRAIN_ALWAYS_ASK:   return "always";
    case BUDDY_BRAIN_HOME_AI:      return "home";
    default:                       return "unknown";
    }
}

static const char *reset_reason_name(esp_reset_reason_t reason)
{
    switch (reason) {
    case ESP_RST_POWERON:   return "power-on";
    case ESP_RST_EXT:       return "external";
    case ESP_RST_SW:        return "software";
    case ESP_RST_PANIC:     return "panic";
    case ESP_RST_INT_WDT:   return "interrupt-watchdog";
    case ESP_RST_TASK_WDT:  return "task-watchdog";
    case ESP_RST_WDT:       return "watchdog";
    case ESP_RST_DEEPSLEEP: return "deep-sleep";
    case ESP_RST_BROWNOUT:  return "brownout";
    case ESP_RST_SDIO:      return "sdio";
    case ESP_RST_USB:       return "usb";
    case ESP_RST_JTAG:      return "jtag";
    default:                return "unknown";
    }
}

static void command_status(const char *argument)
{
    (void)argument;
    const buddy_settings_t *settings = buddy_settings_get();

    begin_reply("status", true);
    printf(",\"firmware\":");
    print_json_string(BUDDY_FIRMWARE_NAME);
    printf(",\"version\":");
    print_json_string(BUDDY_FIRMWARE_VERSION);
    printf(",\"uptime_s\":%lld", (long long)(esp_timer_get_time() / 1000000));
    printf(",\"reset_reason\":");
    print_json_string(reset_reason_name(esp_reset_reason()));
    printf(",\"heap_internal\":%u,\"heap_internal_min\":%u,\"heap_psram\":%u",
           (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
           (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
           (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));

    /* The SSID and password stay on the box; the state and address are enough. */
    printf(",\"wifi\":");
    print_json_string(wifi_state_name(net_wifi_get_state()));
    if (net_wifi_is_connected()) {
        printf(",\"ip\":");
        print_json_string(net_wifi_get_ip_address());
    }

    printf(",\"volume\":%u,\"brightness\":%u",
           (unsigned)hw_speaker_get_volume(), (unsigned)settings->screen_brightness);
    printf(",\"language\":");
    print_json_string(settings->language == BUDDY_LANGUAGE_CHINESE ? "zh" : "en");
    printf(",\"brain\":");
    print_json_string(brain_mode_name(settings->brain_mode));
    printf(",\"cloud_key\":%s", buddy_settings_has_cloud_credentials() ? "true" : "false");
    printf(",\"cloud_requests_left\":%u", (unsigned)voice_assistant_requests_remaining());
    printf(",\"timer_s\":%lu", (unsigned long)buddy_timer_seconds_remaining());

    printf(",\"dock\":%s", hw_sensors_is_dock_present() ? "true" : "false");
    const hw_sensor_reading_t reading = hw_sensors_read_environment();
    if (reading.valid) {
        printf(",\"temperature_c\":%.1f,\"humidity\":%.1f",
               (double)reading.temperature_celsius, (double)reading.humidity_percent);
    }
    end_reply();
}

static void command_selftest(const char *argument)
{
    (void)argument;
    static buddy_selftest_report_t report;   /* ~1.5 KB; kept off the stack. */
    static const char *result_names[] = { "pass", "warn", "fail" };

    buddy_selftest_run(&report);

    begin_reply("selftest", report.failed_count == 0);
    printf(",\"passed\":%u,\"warnings\":%u,\"failed\":%u,\"checks\":[",
           report.passed_count, report.warning_count, report.failed_count);
    for (uint8_t i = 0; i < report.check_count; i++) {
        const buddy_check_t *check = &report.checks[i];
        printf("%s{\"name\":", (i == 0) ? "" : ",");
        print_json_string(check->name);
        printf(",\"detail\":");
        print_json_string(check->detail);
        printf(",\"result\":");
        print_json_string(result_names[check->result <= BUDDY_CHECK_FAILED ? check->result : 2]);
        fputs("}", stdout);
    }
    putchar(']');
    end_reply();
}

static void command_phrases(const char *argument)
{
    (void)argument;
    const buddy_language_t language = buddy_settings_get()->language;
    bool first = true;

    begin_reply("phrases", true);
    printf(",\"phrases\":[");
    for (uint16_t i = 0; i < voice_commands_get_count(); i++) {
        const voice_command_t *command = voice_commands_get(i);
        if (command == NULL || command->language != language) {
            continue;
        }
        if (!first) {
            putchar(',');
        }
        first = false;
        print_json_string(command->phrase);
    }
    putchar(']');
    end_reply();
}

static void command_say(const char *argument)
{
    if (argument == NULL || argument[0] == '\0') {
        reply_error("say", "usage: say <phrase>");
        return;
    }

    /*
     * Only exact phrases from the table, compared without regard to case. This
     * is the same vocabulary the microphone path recognises, so anything that
     * works here also works out loud, and nothing typed can reach a code path
     * that speech cannot.
     */
    const buddy_language_t language = buddy_settings_get()->language;
    for (uint16_t i = 0; i < voice_commands_get_count(); i++) {
        const voice_command_t *command = voice_commands_get(i);
        if (command == NULL || command->language != language) {
            continue;
        }
        if (strcasecmp(command->phrase, argument) == 0) {
            if (!voice_assistant_handle_typed_command(command->action, command->phrase)) {
                reply_error("say", "busy, try again in a moment");
                return;
            }
            begin_reply("say", true);
            printf(",\"phrase\":");
            print_json_string(command->phrase);
            end_reply();
            return;
        }
    }
    reply_error("say", "not an offline phrase; send \"phrases\" for the list");
}

static void command_face(const char *argument)
{
    face_emotion_t emotion;

    if (argument == NULL || !face_emotion_from_name(argument, strlen(argument), &emotion)) {
        reply_error("face", "unknown expression; send \"faces\" for the list");
        return;
    }
    ui_screen_face_set_emotion(emotion);
    begin_reply("face", true);
    printf(",\"face\":");
    print_json_string(face_emotion_get_name(emotion));
    end_reply();
}

static void command_faces(const char *argument)
{
    (void)argument;
    begin_reply("faces", true);
    printf(",\"faces\":[");
    for (int i = 0; i < FACE_EMOTION_COUNT; i++) {
        if (i > 0) {
            putchar(',');
        }
        print_json_string(face_emotion_get_name((face_emotion_t)i));
    }
    putchar(']');
    end_reply();
}

/**
 * @brief Parse a whole decimal number within [low, high].
 */
static bool parse_number(const char *text, long low, long high, long *out)
{
    if (text == NULL || text[0] == '\0') {
        return false;
    }
    char *end = NULL;
    const long value = strtol(text, &end, 10);
    if (end == text || *end != '\0' || value < low || value > high) {
        return false;
    }
    *out = value;
    return true;
}

static void command_volume(const char *argument)
{
    long value;
    if (!parse_number(argument, 0, 100, &value)) {
        reply_error("volume", "usage: volume <0-100>");
        return;
    }
    buddy_settings_get()->speaker_volume = (uint8_t)value;
    hw_speaker_set_volume((uint8_t)value);
    buddy_settings_save();
    begin_reply("volume", true);
    printf(",\"volume\":%ld", value);
    end_reply();
}

static void command_brightness(const char *argument)
{
    long value;
    if (!parse_number(argument, 10, 100, &value)) {
        reply_error("brightness", "usage: brightness <10-100>");
        return;
    }
    buddy_settings_get()->screen_brightness = (uint8_t)value;
    hw_display_set_brightness((uint8_t)value);
    buddy_settings_save();
    begin_reply("brightness", true);
    printf(",\"brightness\":%ld", value);
    end_reply();
}

static void command_timer(const char *argument)
{
    long minutes;

    if (argument != NULL && strcasecmp(argument, "cancel") == 0) {
        const bool was_running = buddy_timer_cancel();
        begin_reply("timer", true);
        printf(",\"cancelled\":%s", was_running ? "true" : "false");
        end_reply();
        return;
    }
    if (!parse_number(argument, 1, BUDDY_TIMER_MAX_MINUTES, &minutes)) {
        reply_error("timer", "usage: timer <minutes> | timer cancel");
        return;
    }
    if (buddy_timer_start((uint16_t)minutes) != ESP_OK) {
        reply_error("timer", "could not start the timer");
        return;
    }
    begin_reply("timer", true);
    printf(",\"minutes\":%ld", minutes);
    end_reply();
}

static void command_reboot(const char *argument)
{
    (void)argument;
    begin_reply("reboot", true);
    end_reply();
    vTaskDelay(pdMS_TO_TICKS(100));   /* Let the answer leave the port. */
    esp_restart();
}

static void command_help(const char *argument);

typedef struct {
    const char *name;
    void (*run)(const char *argument);
    const char *usage;
} console_command_t;

static const console_command_t s_commands[] = {
    { "status",     command_status,     "status" },
    { "selftest",   command_selftest,   "selftest" },
    { "phrases",    command_phrases,    "phrases" },
    { "say",        command_say,        "say <phrase>" },
    { "face",       command_face,       "face <name>" },
    { "faces",      command_faces,      "faces" },
    { "volume",     command_volume,     "volume <0-100>" },
    { "brightness", command_brightness, "brightness <10-100>" },
    { "timer",      command_timer,      "timer <minutes> | timer cancel" },
    { "reboot",     command_reboot,     "reboot" },
    { "help",       command_help,       "help" },
};

static void command_help(const char *argument)
{
    (void)argument;
    begin_reply("help", true);
    printf(",\"commands\":[");
    for (size_t i = 0; i < sizeof(s_commands) / sizeof(s_commands[0]); i++) {
        if (i > 0) {
            putchar(',');
        }
        print_json_string(s_commands[i].usage);
    }
    putchar(']');
    end_reply();
}

/**
 * @brief Split a line into its command word and the rest, and run it.
 */
static void run_line(char *line)
{
    /* Trim both ends. */
    while (isspace((unsigned char)*line)) {
        line++;
    }
    size_t length = strlen(line);
    while (length > 0 && isspace((unsigned char)line[length - 1])) {
        line[--length] = '\0';
    }
    if (length == 0) {
        return;
    }

    char *argument = line;
    while (*argument != '\0' && !isspace((unsigned char)*argument)) {
        argument++;
    }
    if (*argument != '\0') {
        *argument++ = '\0';
        while (isspace((unsigned char)*argument)) {
            argument++;
        }
    }

    for (size_t i = 0; i < sizeof(s_commands) / sizeof(s_commands[0]); i++) {
        if (strcasecmp(s_commands[i].name, line) == 0) {
            s_commands[i].run(argument);
            return;
        }
    }
    reply_error(line, "unknown command; send \"help\"");
}

/* ------------------------------------------------------------------------ */
/* Reading lines                                                             */
/* ------------------------------------------------------------------------ */

#if CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG

static void console_task(void *argument)
{
    (void)argument;
    char line[CONSOLE_LINE_MAX_LEN];
    size_t length = 0;
    bool overflowed = false;

    while (true) {
        uint8_t chunk[32];
        const int count = usb_serial_jtag_read_bytes(chunk, sizeof(chunk), portMAX_DELAY);

        for (int i = 0; i < count; i++) {
            const char c = (char)chunk[i];

            if (c == '\r' || c == '\n') {
                if (overflowed) {
                    reply_error("", "line too long");
                } else if (length > 0) {
                    line[length] = '\0';
                    run_line(line);
                }
                length = 0;
                overflowed = false;
            } else if (length < sizeof(line) - 1) {
                line[length++] = c;
            } else {
                overflowed = true;
            }
        }
    }
}

esp_err_t buddy_console_start(void)
{
    /*
     * Until the driver is installed the port can only send. Installing it and
     * pointing stdout at it as well keeps the log and the answers on one
     * ordered stream instead of two writers racing for the same FIFO.
     */
    usb_serial_jtag_driver_config_t config = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
    esp_err_t err = usb_serial_jtag_driver_install(&config);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "USB serial driver did not install: %s", esp_err_to_name(err));
        return err;
    }
    usb_serial_jtag_vfs_use_driver();

    if (xTaskCreatePinnedToCore(console_task, "console", 4 * 1024, NULL, 2, NULL, 0) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "Serial console ready; send \"help\"");
    return ESP_OK;
}

#else

esp_err_t buddy_console_start(void)
{
    ESP_LOGI(TAG, "Serial console needs CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG");
    return ESP_ERR_NOT_SUPPORTED;
}

#endif
