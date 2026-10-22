/*
 * buddy_presence.h — Noticing when somebody walks up to the desk.
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 *
 * The sensor dock carries an AT581X microwave radar alongside the temperature
 * and humidity parts. It has been powered up and enabled since the first build
 * and nothing has ever read it — hw_sensors_is_someone_nearby() had no callers.
 * This gives it a job.
 *
 * The behaviour is small on purpose: when the buddy has been alone for a while
 * and somebody arrives, it looks up. No sound, no greeting out loud. A box that
 * announces itself every time you walk past your own desk stops being charming
 * within about a day, whereas one that quietly notices you is the difference
 * between an appliance and a pet.
 */

#pragma once

#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Called when somebody arrives after the desk has been empty.
 */
typedef void (*buddy_presence_cb_t)(void);

/**
 * @brief Start watching the radar. Harmless when no dock is attached.
 *
 * @param callback  Told about arrivals. May be NULL to just track state.
 */
esp_err_t buddy_presence_start(buddy_presence_cb_t callback);

/**
 * @brief Was somebody in front of the box at the last check?
 */
bool buddy_presence_is_someone_there(void);

#ifdef __cplusplus
}
#endif
