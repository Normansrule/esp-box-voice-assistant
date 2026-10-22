/*
 * buddy_timer.h — A kitchen timer that needs no network.
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 *
 * "Set a timer for ten minutes" is the single most useful thing a box on a desk
 * can do, and it is one of the few genuinely useful things that needs nothing
 * but a clock — no Wi-Fi, no account, no cloud. So the buddy owns it outright.
 *
 * The countdown itself is deliberately dumb: a target time, a one-second tick
 * for the on-screen badge, and a callback when it lands. Everything about how
 * it is announced — the words, the face, the chime — belongs to the assistant,
 * not here.
 *
 * Only one timer runs at a time. Two would need names to tell them apart, and
 * "cancel the pasta one" is well past what the phrase recogniser can hear.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Longest timer that can be set, in minutes. */
#define BUDDY_TIMER_MAX_MINUTES     180

/**
 * @brief Called from a task context when the countdown reaches zero.
 */
typedef void (*buddy_timer_expired_cb_t)(void);

/**
 * @brief Called once a second while a timer runs, and once when it stops.
 *
 * @param seconds_left  Remaining whole seconds; zero when nothing is running.
 * @param running       False on the tick that reports the timer has ended.
 */
typedef void (*buddy_timer_tick_cb_t)(uint32_t seconds_left, bool running);

/**
 * @brief Prepare the timer. Call once at start-up.
 */
esp_err_t buddy_timer_init(void);

/**
 * @brief Who to tell when a countdown finishes.
 */
void buddy_timer_set_expired_callback(buddy_timer_expired_cb_t callback);

/**
 * @brief Who to tell each second, so the screen can show the count.
 */
void buddy_timer_set_tick_callback(buddy_timer_tick_cb_t callback);

/**
 * @brief Start a countdown, replacing any that is already running.
 *
 * @param minutes  1 to BUDDY_TIMER_MAX_MINUTES. Anything outside is rejected.
 * @return ESP_OK when the countdown started.
 */
esp_err_t buddy_timer_start(uint16_t minutes);

/**
 * @brief Stop the countdown. Safe to call when nothing is running.
 *
 * @return true if a timer was actually cancelled.
 */
bool buddy_timer_cancel(void);

/**
 * @brief Is a countdown in progress?
 */
bool buddy_timer_is_running(void);

/**
 * @brief Whole seconds remaining, or zero when nothing is running.
 */
uint32_t buddy_timer_seconds_remaining(void);

/**
 * @brief Write the remaining time as people say it — "4 minutes 20 seconds".
 *
 * Rounds nothing and abbreviates nothing: this text is both spoken and shown,
 * and "4m20s" reads badly out loud.
 *
 * @param destination  Buffer for the text.
 * @param size         Bytes available, including the terminator.
 * @return true when a timer was running and something was written.
 */
bool buddy_timer_describe_remaining(char *destination, size_t size);

/**
 * @brief Format seconds as a clock for the on-screen badge — "09:59".
 *
 * Separate from the sentence above on purpose: the badge wants something
 * narrow and monospaced, and a sentence wants words.
 *
 * @param seconds      Seconds to render.
 * @param destination  Buffer for the text.
 * @param size         Bytes available, including the terminator.
 */
void buddy_timer_format_clock(uint32_t seconds, char *destination, size_t size);

/**
 * @brief Read a duration out of a recognised phrase.
 *
 * MultiNet can only match phrases registered up front, so the durations are
 * spelled out one per phrase in voice_commands.c and the number is read back
 * out here rather than parsed from free speech.
 *
 * @param phrase   The phrase that matched, e.g. "set a timer for ten minutes".
 * @param minutes  Receives the duration.
 * @return true when a duration was found.
 */
bool buddy_timer_minutes_from_phrase(const char *phrase, uint16_t *minutes);

#ifdef __cplusplus
}
#endif
