/*
 * ui_glance_logic.c — The text on the glance page. See the header.
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */

#include <stdio.h>

#include "ui_glance_logic.h"

static int round_to_int(float value)
{
    return (int)((value < 0.0f) ? (value - 0.5f) : (value + 0.5f));
}

static void set(ui_glance_tile_t *out, const char *value, const char *caption)
{
    snprintf(out->value, sizeof(out->value), "%s", value);
    snprintf(out->caption, sizeof(out->caption), "%s", caption);
}

void ui_glance_temperature(bool valid, float celsius, bool fahrenheit, ui_glance_tile_t *out)
{
    if (out == NULL) {
        return;
    }
    if (!valid || celsius < -60.0f || celsius > 120.0f) {
        set(out, "--", "Room temp");
        return;
    }
    const float shown = fahrenheit ? (celsius * 9.0f / 5.0f + 32.0f) : celsius;
    snprintf(out->value, sizeof(out->value), "%d°%c", round_to_int(shown), fahrenheit ? 'F' : 'C');
    snprintf(out->caption, sizeof(out->caption), "Room temp");
}

void ui_glance_humidity(bool valid, float percent, ui_glance_tile_t *out)
{
    if (out == NULL) {
        return;
    }
    if (!valid) {
        set(out, "--", "Humidity");
        return;
    }
    int value = round_to_int(percent);
    value = (value < 0) ? 0 : (value > 100) ? 100 : value;
    snprintf(out->value, sizeof(out->value), "%d%%", value);
    snprintf(out->caption, sizeof(out->caption), "Humidity");
}

void ui_glance_battery(bool available, bool cell_detected, uint8_t percent_est,
                       bool low, ui_glance_tile_t *out)
{
    if (out == NULL) {
        return;
    }
    if (!available) {
        set(out, "--", "No dock");
        return;
    }
    if (!cell_detected) {
        set(out, "--", "No cell");
        return;
    }
    const unsigned pct = (percent_est > 100) ? 100u : percent_est;
    snprintf(out->value, sizeof(out->value), "~%u%%", pct);
    snprintf(out->caption, sizeof(out->caption), low ? "Battery low" : "Battery est.");
}
