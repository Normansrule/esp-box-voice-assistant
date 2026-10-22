# ESP-BOX Desk Buddy — top-level project file.
#
# This is a standalone ESP-IDF project, not an example inside a larger
# repository. The board support package sits in components/bsp and is picked up
# automatically: ESP-IDF always scans <project>/components for components, so no
# EXTRA_COMPONENT_DIRS is needed.
#
# Build it from an ESP-IDF terminal:
#
#     idf.py set-target esp32s3
#     idf.py build flash monitor
#
# Requires ESP-IDF v5.1 or newer. v5.5.x is what this was written against.
# ESP-IDF v6.0 will NOT work: it removed the legacy I2C driver that the sensor
# dock support in components/bsp still uses.

cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

add_compile_options(-fdiagnostics-color=always
                    -Wno-ignored-qualifiers
                    -Wno-deprecated-declarations
                    -Wno-unused-but-set-variable)

project(esp_box_desk_buddy)
