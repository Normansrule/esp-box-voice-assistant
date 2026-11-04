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

# The OpenAI client is a downloaded component (managed_components), so its
# source can't be edited here. It defines helper functions for API features
# Desk Buddy doesn't use, which produces a page of harmless "defined but not
# used" warnings. Silence just that warning, just for that one component.
idf_build_get_property(desk_buddy_components BUILD_COMPONENTS)
if("espressif__openai" IN_LIST desk_buddy_components)
    idf_component_get_property(openai_lib espressif__openai COMPONENT_LIB)
    target_compile_options(${openai_lib} PRIVATE -Wno-unused-function)
endif()
