/*
 * face_animator.c — Blinking, idling, reacting and lip-syncing.
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 *
 * Four LVGL timers do all the work:
 *
 *   blink timer      fires at random intervals and squashes both eyes shut for
 *                    a fraction of a second
 *   idle timer       fires less often and kicks off a scripted antic
 *   antic timer      steps through the script one pose at a time
 *   lip-sync timer   runs only while speaking, sampling the speaker's output
 *                    level and opening the mouth by that much
 *
 * Everything else is bookkeeping to make sure those four do not fight.
 */

#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"

#include "buddy_config.h"
#include "face_animator.h"
#include "face_view.h"
#include "hw_speaker.h"

static const char *TAG = "face_anim";

/* ------------------------------------------------------------------------ */
/* State                                                                     */
/* ------------------------------------------------------------------------ */

static lv_timer_t *s_blink_timer;
static lv_timer_t *s_idle_timer;
static lv_timer_t *s_antic_timer;
static lv_timer_t *s_lip_sync_timer;
static lv_timer_t *s_flash_timer;

/** The mood the face returns to whenever nothing else is going on. */
static face_emotion_t s_resting_emotion = FACE_EMOTION_NEUTRAL;

/** The antic currently being performed, and how far through it we are. */
static const face_antic_t *s_active_antic;
static uint8_t s_active_antic_step;

static bool s_speaking;
static bool s_idle_antics_enabled = true;
static bool s_started;

/** Microsecond timestamp of the last human interaction, for the sleepy timer. */
static int64_t s_last_activity_us;

/* Forward declarations for the timer callbacks. */
static void blink_timer_cb(lv_timer_t *timer);
static void idle_timer_cb(lv_timer_t *timer);
static void antic_timer_cb(lv_timer_t *timer);
static void lip_sync_timer_cb(lv_timer_t *timer);
static void flash_timer_cb(lv_timer_t *timer);

/* ------------------------------------------------------------------------ */
/* Helpers                                                                   */
/* ------------------------------------------------------------------------ */

/**
 * @brief Uniform random integer in [low, high].
 */
static uint32_t random_between(uint32_t low, uint32_t high)
{
    if (high <= low) {
        return low;
    }
    return low + (esp_random() % (high - low + 1));
}

/**
 * @brief Re-arm the blink timer with a fresh random delay.
 */
static void schedule_next_blink(void)
{
    if (s_blink_timer != NULL) {
        lv_timer_set_period(s_blink_timer,
                            random_between(FACE_BLINK_INTERVAL_MIN_MS,
                                           FACE_BLINK_INTERVAL_MAX_MS));
        lv_timer_reset(s_blink_timer);
    }
}

/**
 * @brief Re-arm the idle-antic timer with a fresh random delay.
 */
static void schedule_next_idle_antic(void)
{
    if (s_idle_timer != NULL) {
        lv_timer_set_period(s_idle_timer,
                            random_between(FACE_IDLE_ANTIC_INTERVAL_MIN_MS,
                                           FACE_IDLE_ANTIC_INTERVAL_MAX_MS));
        lv_timer_reset(s_idle_timer);
    }
}

/**
 * @brief Glide back to whatever the resting expression currently is.
 */
static void return_to_resting_emotion(uint16_t duration_ms)
{
    face_view_animate_to(face_emotion_get_pose(s_resting_emotion), duration_ms);
}

/**
 * @brief Stop any antic in progress without changing the expression.
 */
static void cancel_active_antic(void)
{
    s_active_antic = NULL;
    s_active_antic_step = 0;
    if (s_antic_timer != NULL) {
        lv_timer_del(s_antic_timer);
        s_antic_timer = NULL;
    }
}

/* ------------------------------------------------------------------------ */
/* Blinking                                                                  */
/* ------------------------------------------------------------------------ */

/**
 * @brief Second half of a blink: open the eyes again.
 */
static void blink_reopen_cb(lv_timer_t *timer)
{
    (void)timer; /* Created with a repeat count of 1, so LVGL frees it for us. */

    face_pose_t open_pose;
    face_view_get_target_pose(&open_pose);
    open_pose.left_eye_shut = false;
    open_pose.right_eye_shut = false;
    face_view_animate_to(&open_pose, FACE_BLINK_OPEN_MS);
}

void face_animator_blink_now(void)
{
    /*
     * A blink is deliberately built on top of whatever pose is showing rather
     * than on a fixed "blink pose": that way the buddy can blink mid-antic, or
     * while looking sideways, without losing its place.
     */
    face_pose_t shut_pose;
    face_view_get_target_pose(&shut_pose);

    /* If both eyes are already shut (asleep, winking) there is nothing to do. */
    if (shut_pose.left_eye_shut && shut_pose.right_eye_shut) {
        return;
    }

    shut_pose.left_eye_shut = true;
    shut_pose.right_eye_shut = true;
    face_view_animate_to(&shut_pose, FACE_BLINK_CLOSE_MS);

    /* One-shot timer that opens the eyes again once they are closed. */
    lv_timer_t *reopen = lv_timer_create(blink_reopen_cb, FACE_BLINK_CLOSE_MS + 20, NULL);
    lv_timer_set_repeat_count(reopen, 1);
}

static void blink_timer_cb(lv_timer_t *timer)
{
    (void)timer;

    /* Do not blink over the top of a mid-flight antic — it looks twitchy. */
    if (s_active_antic == NULL && !s_speaking) {
        face_animator_blink_now();
    }
    schedule_next_blink();
}

/* ------------------------------------------------------------------------ */
/* Antics                                                                    */
/* ------------------------------------------------------------------------ */

/**
 * @brief Move the face into one step of the running antic.
 */
static void run_current_antic_step(void)
{
    if (s_active_antic == NULL) {
        return;
    }

    if (s_active_antic_step >= s_active_antic->step_count) {
        /* Script finished: settle back down and let the idle timer resume. */
        cancel_active_antic();
        return_to_resting_emotion(FACE_EXPRESSION_TRANSITION_MS);
        schedule_next_idle_antic();
        return;
    }

    const face_antic_step_t *step = &s_active_antic->steps[s_active_antic_step];

    /* Start from the emotion's own pose, then apply the step's overrides. */
    face_pose_t pose = *face_emotion_get_pose(step->emotion);
    pose.look_x        = step->look_x;
    pose.look_y        = step->look_y;
    pose.face_offset_x = step->face_offset_x;
    pose.face_offset_y = step->face_offset_y;

    face_view_animate_to(&pose, step->transition_ms);

    /* Schedule the next step for after this one has glided and been held. */
    const uint32_t step_total_ms = (uint32_t)step->transition_ms + step->hold_ms;
    s_active_antic_step++;

    if (s_antic_timer == NULL) {
        s_antic_timer = lv_timer_create(antic_timer_cb,
                                        (step_total_ms > 10) ? step_total_ms : 10, NULL);
    } else {
        lv_timer_set_period(s_antic_timer, (step_total_ms > 10) ? step_total_ms : 10);
        lv_timer_reset(s_antic_timer);
    }
}

static void antic_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    run_current_antic_step();
}

void face_animator_play_antic(uint8_t index)
{
    const face_antic_t *antic = face_antic_get(index);
    if (antic == NULL || antic->step_count == 0) {
        return;
    }

    /* An antic while speaking would fight the lip-sync, so skip it. */
    if (s_speaking) {
        return;
    }

    cancel_active_antic();
    s_active_antic = antic;
    s_active_antic_step = 0;

    ESP_LOGD(TAG, "Antic: %s", antic->name);
    run_current_antic_step();
}

void face_animator_play_random_antic(void)
{
    const uint8_t count = face_antic_get_count();
    if (count == 0) {
        return;
    }
    face_animator_play_antic((uint8_t)(esp_random() % count));
}

static void idle_timer_cb(lv_timer_t *timer)
{
    (void)timer;

    if (!s_idle_antics_enabled || s_speaking || s_active_antic != NULL) {
        schedule_next_idle_antic();
        return;
    }

    /*
     * After a long stretch of being ignored the buddy drifts into a sleepy
     * expression instead of performing. A tap or a wake word snaps it out.
     */
    const int64_t idle_us = esp_timer_get_time() - s_last_activity_us;
    const int64_t sleepy_after_us = (int64_t)FACE_SLEEPY_AFTER_MINUTES * 60 * 1000000;

    if (idle_us > sleepy_after_us && s_resting_emotion == FACE_EMOTION_NEUTRAL) {
        ESP_LOGD(TAG, "Nothing happening for a while - getting sleepy");
        s_resting_emotion = FACE_EMOTION_SLEEPY;
        face_view_animate_to(face_emotion_get_pose(FACE_EMOTION_SLEEPY),
                             FACE_EXPRESSION_TRANSITION_MS * 3);
        schedule_next_idle_antic();
        return;
    }

    face_animator_play_random_antic();
}

/* ------------------------------------------------------------------------ */
/* Emotions                                                                  */
/* ------------------------------------------------------------------------ */

void face_animator_set_emotion(face_emotion_t emotion, uint16_t duration_ms)
{
    if (emotion < 0 || emotion >= FACE_EMOTION_COUNT) {
        emotion = FACE_EMOTION_NEUTRAL;
    }

    cancel_active_antic();

    if (s_flash_timer != NULL) {
        lv_timer_del(s_flash_timer);
        s_flash_timer = NULL;
    }

    s_resting_emotion = emotion;
    return_to_resting_emotion((duration_ms > 0) ? duration_ms
                                                : FACE_EXPRESSION_TRANSITION_MS);
    ESP_LOGD(TAG, "Emotion -> %s", face_emotion_get_name(emotion));
}

face_emotion_t face_animator_get_emotion(void)
{
    return s_resting_emotion;
}

static void flash_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    s_flash_timer = NULL;
    return_to_resting_emotion(FACE_EXPRESSION_TRANSITION_MS);
}

void face_animator_flash_emotion(face_emotion_t emotion, uint16_t hold_ms)
{
    if (emotion < 0 || emotion >= FACE_EMOTION_COUNT) {
        return;
    }

    cancel_active_antic();

    if (s_flash_timer != NULL) {
        lv_timer_del(s_flash_timer);
        s_flash_timer = NULL;
    }

    face_view_animate_to(face_emotion_get_pose(emotion), FACE_EXPRESSION_TRANSITION_MS);

    const uint32_t total_ms = FACE_EXPRESSION_TRANSITION_MS + hold_ms;
    s_flash_timer = lv_timer_create(flash_timer_cb, total_ms, NULL);
    lv_timer_set_repeat_count(s_flash_timer, 1);
}

/* ------------------------------------------------------------------------ */
/* Lip sync                                                                  */
/* ------------------------------------------------------------------------ */

static void lip_sync_timer_cb(lv_timer_t *timer)
{
    (void)timer;

    /*
     * hw_speaker measures the loudness of every buffer it pushes to the codec
     * and reports it as 0-100. Mapping that straight onto the mouth opening
     * makes the buddy mouth along with its own voice — loud syllables open the
     * mouth wide, pauses close it.
     */
    const uint8_t level = hw_speaker_get_output_level();

    int32_t opening = (level * FACE_MOUTH_MAX_OPEN_HEIGHT) / 100;

    /* Never fully shut while talking; a little movement always looks alive. */
    if (opening < 4) {
        opening = 4;
    }
    if (opening > FACE_MOUTH_MAX_OPEN_HEIGHT) {
        opening = FACE_MOUTH_MAX_OPEN_HEIGHT;
    }

    face_view_set_mouth_opening((int16_t)opening);
}

void face_animator_set_speaking(bool speaking)
{
    if (speaking == s_speaking) {
        return;
    }
    s_speaking = speaking;

    if (speaking) {
        cancel_active_antic();
        face_view_animate_to(face_emotion_get_pose(FACE_EMOTION_SPEAKING),
                             FACE_EXPRESSION_TRANSITION_MS / 2);

        if (s_lip_sync_timer == NULL) {
            s_lip_sync_timer = lv_timer_create(lip_sync_timer_cb,
                                               FACE_LIP_SYNC_REFRESH_MS, NULL);
        }
        ESP_LOGD(TAG, "Lip sync on");
    } else {
        if (s_lip_sync_timer != NULL) {
            lv_timer_del(s_lip_sync_timer);
            s_lip_sync_timer = NULL;
        }
        return_to_resting_emotion(FACE_EXPRESSION_TRANSITION_MS);
        schedule_next_idle_antic();
        ESP_LOGD(TAG, "Lip sync off");
    }
}

/* ------------------------------------------------------------------------ */
/* Lifecycle                                                                 */
/* ------------------------------------------------------------------------ */

void face_animator_set_idle_antics_enabled(bool enabled)
{
    s_idle_antics_enabled = enabled;
    if (enabled) {
        schedule_next_idle_antic();
    }
}

void face_animator_notice_activity(void)
{
    s_last_activity_us = esp_timer_get_time();

    /* If the buddy had dozed off, perk it back up. */
    if (s_resting_emotion == FACE_EMOTION_SLEEPY || s_resting_emotion == FACE_EMOTION_ASLEEP) {
        face_animator_set_emotion(FACE_EMOTION_NEUTRAL, FACE_EXPRESSION_TRANSITION_MS);
    }
}

esp_err_t face_animator_start(void)
{
    if (s_started) {
        return ESP_OK;
    }

    s_last_activity_us = esp_timer_get_time();

    s_blink_timer = lv_timer_create(blink_timer_cb, FACE_BLINK_INTERVAL_MIN_MS, NULL);
    s_idle_timer  = lv_timer_create(idle_timer_cb, FACE_IDLE_ANTIC_INTERVAL_MIN_MS, NULL);
    if (s_blink_timer == NULL || s_idle_timer == NULL) {
        ESP_LOGE(TAG, "Could not create the face timers");
        return ESP_ERR_NO_MEM;
    }

    schedule_next_blink();
    schedule_next_idle_antic();

    s_started = true;
    ESP_LOGI(TAG, "Face animator running");
    return ESP_OK;
}
