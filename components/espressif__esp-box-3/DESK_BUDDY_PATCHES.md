# Local copy of espressif/esp-box-3 1.1.3 (patched)

This folder overrides the registry copy in `managed_components/`. ESP-IDF picks
a component in the project's `components/` folder over a managed one with the
same name.

## Why

BSP 1.1.3 drives I2C with the **legacy** driver (`i2c_driver_install`). Its
`^1` dependency on `esp_lcd_touch_gt911` / `esp_lcd_touch_tt21100` now
resolves to 1.2.x, whose `ESP_LCD_TOUCH_IO_I2C_*_CONFIG()` macros set
`.scl_speed_hz = 100000` for the new `i2c_master` driver. On ESP-IDF >= 5.2
the legacy panel-IO path (`esp_lcd_new_panel_io_i2c_v1`) rejects that with
`ESP_ERR_INVALID_ARG` ("scl_speed_hz is not need to set in legacy i2c_lcd
driver"), and with `CONFIG_BSP_ERROR_CHECK=y` the BSP aborts, so the box
boot-loops.

The whole project (codec, AHT20, AT581x, ICM42670, BSP) stays on the legacy
I2C driver. Mixing in `i2c_master` on the same port is not allowed, so the
fix keeps everything legacy instead of half-migrating.

## Changes (search for `DESK BUDDY PATCH` in esp-box-3.c)

1. `bsp_touch_new`: clear `scl_speed_hz` before creating the legacy panel IO.
2. `bsp_display_indev_init` / `bsp_display_start_with_config`: a touch
   failure logs a warning and the display is still returned.
3. Touch sleep/wake and the on-screen home button tolerate a missing touch
   handle instead of asserting.

## Removing this copy

Delete this folder once the project migrates to BSP 2.x+ (new I2C driver)
for every I2C device, or once an upstream 1.x BSP handles this.
