/*
 * face_view.h — The LVGL objects the face is actually made of.
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 *
 * This module owns pixels and nothing else. It knows how to turn a face_pose_t
 * into positions and sizes for a handful of LVGL widgets, and how to glide
 * smoothly from one pose to another. It has no opinion about WHEN the face
 * should change — that is face_animator's job.
 *
 * How the face is built out of ordinary LVGL widgets:
 *
 *   eyes    Two red rounded rectangles. Each one carries two background-coloured
 *           circles as children. Sliding a circle up from below carves a curved
 *           "^" bite out of the eye, which is how the delighted squint and the
 *           sleepy half-lid are drawn without any bitmaps.
 *
 *   brows   Two lv_line objects of three points each. Moving the inner point up
 *           or down tilts the brow, which carries most of the expression.
 *
 *   mouth   One lv_line bent into a parabola (smile up, frown down) plus one
 *           red pill-shaped object that grows out of it when the mouth opens.
 *
 *   blush   Two pale circles whose opacity rides along with the mood.
 *
 * IMPORTANT: every function here must be called with the LVGL lock held. Use
 * hw_display_lock() / hw_display_unlock() from any task that is not LVGL's own.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "lvgl.h"
#include "face_pose.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Build the face widgets inside @p parent and show the neutral pose.
 *
 * @param parent  Container the face is drawn into, usually the active screen.
 * @return ESP_OK on success.
 */
esp_err_t face_view_create(lv_obj_t *parent);

/**
 * @brief Snap the face to a pose immediately, with no animation.
 */
void face_view_apply_pose(const face_pose_t *pose);

/**
 * @brief Glide from wherever the face is now to @p target.
 *
 * Starting a new glide cancels any glide already in progress, so calls can be
 * chained freely without the face jumping.
 *
 * @param target       Pose to end up in.
 * @param duration_ms  Length of the glide. Zero snaps immediately.
 */
void face_view_animate_to(const face_pose_t *target, uint16_t duration_ms);

/**
 * @brief Copy the pose currently on screen.
 *
 * During a glide this is the interpolated, mid-flight pose — which is exactly
 * what you want as the starting point of the next glide.
 */
void face_view_get_current_pose(face_pose_t *out);

/**
 * @brief Copy the pose the face is heading towards (or resting in).
 */
void face_view_get_target_pose(face_pose_t *out);

/**
 * @brief Override just the mouth opening, without disturbing anything else.
 *
 * Used many times a second by the lip-sync so the mouth flaps in time with the
 * audio while the rest of the expression stays put.
 *
 * @param open_height  0 (shut) to FACE_MOUTH_MAX_OPEN_HEIGHT (wide open).
 */
void face_view_set_mouth_opening(int16_t open_height);

/**
 * @brief Shift the whole face up or down, e.g. to make room for a speech bubble.
 *
 * @param pixels       Positive moves the face down, negative moves it up.
 * @param duration_ms  Length of the slide. Zero snaps immediately.
 */
void face_view_set_vertical_shift(int16_t pixels, uint16_t duration_ms);

/**
 * @brief Show or hide the entire face at once.
 */
void face_view_set_hidden(bool hidden);

#ifdef __cplusplus
}
#endif
