# Board support package for the ESP32-S3-BOX family.
#
# WHAT CHANGED FROM THE STOCK ESPRESSIF VERSION
# ---------------------------------------------
# The original decided whether to compile sensor-dock support by checking
# whether the project's folder path contained the string "factory_demo":
#
#     string(REGEX MATCH "factory_demo" PROJECT_IS_FACTORY_DEMO "${PROJECT_DIR}")
#
# That worked only for the one example it shipped with. Renaming or moving the
# project silently swapped in the no-sensor build, and the temperature and
# humidity readings quietly stopped existing with no error to explain it.
#
# Now the decision is made on the only thing that actually matters: whether the
# board is an ESP32-S3-BOX-3, which is the model the sensor dock attaches to.

# ---------------------------------------------------------------------------
# Work out which board is being built for.
#
# This runs before Kconfig has been processed, so the board choice is read
# straight out of the project's sdkconfig (or sdkconfig.defaults on a first
# build, or the Kconfig default if neither mentions a board).
# ---------------------------------------------------------------------------
if(EXISTS ${PROJECT_DIR}/sdkconfig)
    message(STATUS "BSP: reading board choice from sdkconfig")
    file(READ ${PROJECT_DIR}/sdkconfig SDKCONFIG_RULE)
elseif(EXISTS ${PROJECT_DIR}/sdkconfig.defaults)
    message(STATUS "BSP: reading board choice from sdkconfig.defaults")
    file(READ ${PROJECT_DIR}/sdkconfig.defaults SDKCONFIG_RULE)
endif()

if(SDKCONFIG_RULE)
    string(REGEX MATCH "CONFIG_BSP_BOARD_ESP32_S3_BOX_Lite=y" COMPILER_TARGET_IS_BOX_LITE "${SDKCONFIG_RULE}")
    string(REGEX MATCH "CONFIG_BSP_BOARD_ESP32_S3_BOX=y" COMPILER_TARGET_IS_ESP_BOX "${SDKCONFIG_RULE}")
    string(REGEX MATCH "CONFIG_BSP_BOARD_ESP32_S3_BOX_3=y" COMPILER_TARGET_IS_ESP_BOX_3 "${SDKCONFIG_RULE}")
endif()

# Nothing said which board, so fall back to whatever Kconfig defaults to.
if(NOT COMPILER_TARGET_IS_BOX_LITE AND NOT COMPILER_TARGET_IS_ESP_BOX AND NOT COMPILER_TARGET_IS_ESP_BOX_3)
    message(STATUS "BSP: no board in sdkconfig, using the Kconfig default")
    file(READ ${CMAKE_CURRENT_LIST_DIR}/Kconfig.projbuild SDKCONFIG_RULE)
    string(REGEX MATCH "default BSP_BOARD_ESP32_S3_BOX_Lite" COMPILER_TARGET_IS_BOX_LITE "${SDKCONFIG_RULE}")
    string(REGEX MATCH "default BSP_BOARD_ESP32_S3_BOX_3" COMPILER_TARGET_IS_ESP_BOX_3 "${SDKCONFIG_RULE}")
    string(REGEX MATCH "default BSP_BOARD_ESP32_S3_\\bBOX\\b$" COMPILER_TARGET_IS_ESP_BOX "${SDKCONFIG_RULE}")
endif()

# ---------------------------------------------------------------------------
# Pick the right sources for that board.
# ---------------------------------------------------------------------------
if(COMPILER_TARGET_IS_ESP_BOX_3)
    message(STATUS "BSP: building for ESP32-S3-BOX-3")
    set(box_alias "-3")
elseif(COMPILER_TARGET_IS_BOX_LITE)
    message(STATUS "BSP: building for ESP32-S3-BOX-Lite")
    set(box_alias "-lite")
    set(bsp_src "src/storage/bsp_sdcard.c")
elseif(COMPILER_TARGET_IS_ESP_BOX)
    message(STATUS "BSP: building for ESP32-S3-BOX")
    set(bsp_src "src/storage/bsp_sdcard.c")
else()
    message(FATAL_ERROR "BSP: could not work out which board to build for. "
                        "Set one with: idf.py menuconfig -> HMI Board Config")
endif()

set(requires "driver" "fatfs")
set(priv_requires "esp-box${box_alias}")

# The sensor dock (temperature, humidity, presence radar) only exists for the
# BOX-3, so its drivers are only pulled in for that board.
if(COMPILER_TARGET_IS_ESP_BOX_3)
    list(APPEND priv_requires "aht20" "at581x")
    list(APPEND bsp_src "src/boards/esp32_bsp_sensor.c")
else()
    list(APPEND bsp_src "src/boards/esp32_bsp_no_sensor.c")
endif()

list(APPEND bsp_src "src/boards/esp32_bsp_board.c")

idf_component_register(
    SRCS ${bsp_src}
    INCLUDE_DIRS "src/boards" "include"
    PRIV_INCLUDE_DIRS "priv_include"
    PRIV_REQUIRES ${priv_requires}
    REQUIRES ${requires})
