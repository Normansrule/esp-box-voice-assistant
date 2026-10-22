/*
 * buddy_console.h — Talking to the buddy over its USB cable.
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 *
 * The box already prints its log over the USB port it is flashed through. This
 * adds the other direction: type a line, get an answer. It is what the flasher
 * page (web/connect/) and the desktop app (desktop/) talk to, and it works just
 * as well from `idf.py monitor` or any serial terminal.
 *
 * THE PROTOCOL
 * ------------
 * One command per line, words separated by spaces:
 *
 *     status                  versions, memory, Wi-Fi, settings, sensors
 *     selftest                run the same checks as Settings -> Diagnostics
 *     phrases                 every offline phrase in the current language
 *     say <phrase>            act as though <phrase> had just been heard
 *     face <name>             change the expression, e.g. "face happy"
 *     faces                   list the expression names
 *     volume <0-100>          speaker volume, saved
 *     brightness <10-100>     backlight, saved
 *     timer <minutes>         start the kitchen timer; "timer cancel" stops it
 *     reboot                  restart the chip
 *     help                    this list
 *
 * Every answer is ONE line that starts with "@buddy " followed by a JSON
 * object, so a program can pick answers out of the log stream without parsing
 * the log itself. Every answer carries "cmd" and "ok"; failures add "error".
 *
 * WHAT IT NEVER PRINTS
 * --------------------
 * The Wi-Fi password, the API key or the cloud base URL. "status" says whether
 * a key is present and nothing more. The console cannot set either: those are
 * entered on the touchscreen, where somebody is physically holding the box.
 */

#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Prefix on every line the console answers with. */
#define BUDDY_CONSOLE_REPLY_PREFIX  "@buddy "

/**
 * @brief Start reading commands from the USB serial port.
 *
 * Call last in app_main(), once everything a command could touch is running.
 * Does nothing (and returns ESP_ERR_NOT_SUPPORTED) when the console is not on
 * the USB-Serial-JTAG port.
 */
esp_err_t buddy_console_start(void);

#ifdef __cplusplus
}
#endif
