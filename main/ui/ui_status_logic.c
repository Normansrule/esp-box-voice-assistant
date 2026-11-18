/*
 * ui_status_logic.c — What the status chip should say. See the header.
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */

#include <stdio.h>

#include "ui_status_logic.h"

ui_status_t ui_status_pick(const ui_status_inputs_t *inputs)
{
    if (inputs == NULL) {
        return UI_STATUS_HIDDEN;
    }
    /*
     * Mute first, always. The listening flag can lag the switch by one audio
     * frame, and the chip must never say "listening" while the switch is on.
     */
    if (inputs->muted) {
        return UI_STATUS_MIC_OFF;
    }
    if (inputs->listening) {
        return UI_STATUS_LISTENING;
    }
    if (inputs->online_busy) {
        return UI_STATUS_ONLINE;
    }
    if (inputs->clock_valid && inputs->clock_enabled) {
        return UI_STATUS_CLOCK;
    }
    return UI_STATUS_HIDDEN;
}

uint8_t ui_status_level_bars(uint16_t peak)
{
    /*
     * Thresholds sit about 8 dB apart, starting above a quiet room's floor
     * (the speech gate typically measures a few hundred in a quiet room).
     */
    static const uint16_t steps[UI_STATUS_LEVEL_BARS] = { 400, 1000, 2500, 6300, 16000 };

    uint8_t bars = 0;
    while (bars < UI_STATUS_LEVEL_BARS && peak >= steps[bars]) {
        bars++;
    }
    return bars;
}

void ui_status_format_clock(int hour24, int minute, char *out, size_t out_len)
{
    if (out == NULL || out_len == 0) {
        return;
    }
    if (hour24 < 0 || hour24 > 23 || minute < 0 || minute > 59) {
        snprintf(out, out_len, "--:--");
        return;
    }
    int hour = hour24 % 12;
    if (hour == 0) {
        hour = 12;
    }
    snprintf(out, out_len, "%d:%02d", hour, minute);
}
