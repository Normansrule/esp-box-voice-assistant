/*
 * ui_status_logic.h — What the status chip should say, as pure functions.
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 *
 * STATUS: VERIFIED IN TEST (host tests). UNTESTED ON DEVICE.
 *
 * The chip on the home screen reports device state, so the decision of what it
 * shows is kept here, free of LVGL, where the host tests can check every
 * combination. The most important rule is the privacy one: when the mute switch
 * is on, the chip says so, whatever else is going on.
 *
 * Every input is a reading of real state taken by the caller (the mute switch,
 * the wake word engine, the cloud brain, the clock). Nothing here guesses.
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** What the chip is showing, in priority order (lowest value wins). */
typedef enum {
    UI_STATUS_MIC_OFF = 0,   /*!< Mute switch on: capture returns silence      */
    UI_STATUS_LISTENING,     /*!< Wake word heard, the recording window is open */
    UI_STATUS_ONLINE,        /*!< A request to the home or cloud brain is out  */
    UI_STATUS_CLOCK,         /*!< Nothing happening and the clock is trusted   */
    UI_STATUS_HIDDEN,        /*!< Nothing worth saying: the chip hides         */
} ui_status_t;

/** Readings of real device state, taken by the caller just before deciding. */
typedef struct {
    bool muted;          /*!< hw_buttons_is_muted()                     */
    bool listening;      /*!< voice_wakeword_is_listening()             */
    bool online_busy;    /*!< voice_cloud_brain_is_busy()               */
    bool clock_valid;    /*!< net_time_is_synchronised()                */
    bool clock_enabled;  /*!< The user has not switched the clock off   */
} ui_status_inputs_t;

/**
 * @brief Pick the one thing the chip should show.
 */
ui_status_t ui_status_pick(const ui_status_inputs_t *inputs);

/** How many level bars the listening chip draws. */
#define UI_STATUS_LEVEL_BARS    5

/**
 * @brief Turn a live microphone peak into a number of lit bars.
 *
 * The steps are roughly logarithmic (about 8 dB apart), because loudness is
 * heard that way: a linear scale would light one bar for normal speech and all
 * five only when shouting.
 *
 * @param peak  Peak absolute sample of the latest audio chunk, 0..32767.
 * @return 0..UI_STATUS_LEVEL_BARS.
 */
uint8_t ui_status_level_bars(uint16_t peak);

/**
 * @brief Format a 12-hour clock with no leading zero, e.g. "9:05" or "12:30".
 *
 * @param hour24  0..23. Out-of-range values give "--:--".
 * @param minute  0..59. Out-of-range values give "--:--".
 * @param out     Destination, at least 6 bytes.
 * @param out_len Size of @p out.
 */
void ui_status_format_clock(int hour24, int minute, char *out, size_t out_len);

#ifdef __cplusplus
}
#endif
