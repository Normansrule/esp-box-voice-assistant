/*
 * hw_buttons.c — The mute switch.
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 *
 * The board support package registers a weak `mute_btn_handler` and calls it
 * whenever the switch on the top edge moves. Defining a strong version of that
 * symbol here quietly replaces the default one — that is the hook the ESP-BOX
 * examples are designed around.
 */

#include "esp_log.h"

#include "bsp/esp-bsp.h"
#include "bsp_board.h"
#include "driver/gpio.h"

#include "hw_buttons.h"
#include "hw_microphone.h"

static const char *TAG = "hw_buttons";

static bool s_muted;
static hw_buttons_mute_changed_cb_t s_mute_callback;

/**
 * @brief Apply a new mute state and tell everyone who cares.
 */
static void apply_mute_state(bool muted)
{
    if (s_muted == muted) {
        return;
    }
    s_muted = muted;

    ESP_LOGI(TAG, "Mute switch: %s", muted ? "MUTED" : "live");

    /* The microphone is silenced first, so nothing can slip through. */
    hw_microphone_set_muted(muted);

    if (s_mute_callback != NULL) {
        s_mute_callback(muted);
    }
}

/**
 * @brief Strong override of the board support package's weak handler.
 *
 * @param handle  Button handle (unused).
 * @param arg     The button_event_t that occurred, passed as a void pointer.
 */
void mute_btn_handler(void *handle, void *arg)
{
    (void)handle;

    const button_event_t event = (button_event_t)arg;

    /*
     * The switch is wired active-low, so "pressed" is the muted position. This
     * matches the mapping the stock ESP-BOX firmware uses.
     */
    apply_mute_state(event == BUTTON_PRESS_DOWN);
}

esp_err_t hw_buttons_init(void)
{
    /*
     * bsp_board_init() has already created the button driver and wired
     * mute_btn_handler above to both edges. All that is left is to read where
     * the switch is sitting right now, since no event fires for that.
     */
    const int level = gpio_get_level(BSP_BUTTON_MUTE_IO);
    const bool muted_now = (level == 0);

    s_muted = !muted_now;      /* Force apply_mute_state() to do its work. */
    apply_mute_state(muted_now);

    ESP_LOGI(TAG, "Buttons ready (mute switch starts %s)", muted_now ? "muted" : "live");
    return ESP_OK;
}

void hw_buttons_set_mute_callback(hw_buttons_mute_changed_cb_t callback)
{
    s_mute_callback = callback;
}

bool hw_buttons_is_muted(void)
{
    return s_muted;
}
