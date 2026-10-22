/*
 * hw_display.c — Panel bring-up and the LVGL lock.
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 *
 * The heavy lifting (SPI bus, panel driver, touch controller, LVGL port task)
 * is done by the ESP-BSP component that ships with the board. This module just
 * configures it, and then hides the BSP behind names that describe intent.
 */

#include "esp_log.h"

#include "bsp/esp-bsp.h"
#include "bsp_board.h"

#include "buddy_config.h"
#include "buddy_settings.h"
#include "face_theme.h"
#include "hw_display.h"

static const char *TAG = "hw_display";

esp_err_t hw_display_init(void)
{
    /*
     * The panel is refreshed in horizontal strips CONFIG_BSP_LCD_DRAW_BUF_HEIGHT
     * pixels tall, and this is where the single most valuable resource on the
     * board gets spent.
     *
     * Draw buffers must be DMA-capable, which on the ESP32-S3 means INTERNAL
     * RAM — the 16 MB of PSRAM cannot hold them. Internal RAM is only ~320 KB
     * in total and everything else fights over the same pool: task stacks, the
     * Wi-Fi driver, LVGL's widget tree, and the I2C driver's little command
     * descriptors.
     *
     *   One buffer,  20 lines:  320 x 20 x 2 = 12.5 KB   <- what we use
     *   Two buffers, 40 lines:  320 x 40 x 2 x 2 = 50 KB
     *
     * The larger setting genuinely does redraw more smoothly. It also left
     * roughly 29 KB of internal RAM free, and at that point i2c_cmd_link_create
     * starts returning NULL — which shows up as a stream of
     *
     *     E i2c: i2c_master_start(1300): i2c command link error
     *
     * every time the temperature is polled, and eventually an abort inside the
     * AHT20 driver, because it asserts that its reads succeed. Opening the
     * settings screen was enough to tip it over, since building that page
     * allocates a few hundred LVGL objects.
     *
     * A slightly less silky face is a fair trade for a buddy that does not
     * reboot. If you ever want the smoother setting back, raise these AND
     * confirm the self-test still reports comfortably more than 60 KB of
     * internal RAM free with the settings screen open.
     */
    bsp_display_cfg_t config = {
        .lvgl_port_cfg = ESP_LVGL_PORT_INIT_CONFIG(),
        .buffer_size = BSP_LCD_H_RES * CONFIG_BSP_LCD_DRAW_BUF_HEIGHT,
        .double_buffer = 0,
        .flags = {
            .buff_dma = true,
        },
    };

    /*
     * Pin LVGL to core 1. Core 0 runs Wi-Fi and the audio front end, both of
     * which are latency-sensitive; keeping rendering off that core stops the
     * face from stuttering whenever the network is busy.
     */
    config.lvgl_port_cfg.task_affinity = 1;

    lv_disp_t *display = bsp_display_start_with_config(&config);
    if (display == NULL) {
        ESP_LOGE(TAG, "Display did not start");
        return ESP_FAIL;
    }

    /* The buddy lives on a clean white field — set it before anything draws. */
    if (hw_display_lock(0)) {
        lv_obj_set_style_bg_color(lv_scr_act(), FACE_COLOR_BACKGROUND, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(lv_scr_act(), LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_clear_flag(lv_scr_act(), LV_OBJ_FLAG_SCROLLABLE);
        hw_display_unlock();
    }

    ESP_LOGI(TAG, "Display ready (%dx%d)", BSP_LCD_H_RES, BSP_LCD_V_RES);
    return ESP_OK;
}

bool hw_display_lock(uint32_t timeout_ms)
{
    return bsp_display_lock(timeout_ms);
}

void hw_display_unlock(void)
{
    bsp_display_unlock();
}

void hw_display_set_brightness(uint8_t percent)
{
    if (percent < 10) {
        percent = 10;
    }
    if (percent > 100) {
        percent = 100;
    }
    bsp_display_brightness_set(percent);
}

void hw_display_set_backlight_on(void)
{
    const buddy_settings_t *settings = buddy_settings_get();
    hw_display_set_brightness(settings->screen_brightness);
}

void hw_display_set_backlight_off(void)
{
    bsp_display_backlight_off();
}
