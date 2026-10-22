/*
 * face_pose.h — The numbers that describe one facial expression.
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 *
 * THE CENTRAL IDEA OF THE WHOLE FACE SYSTEM
 * -----------------------------------------
 * The buddy's face is not a set of images. It is a handful of shapes whose
 * sizes and positions are numbers. A "pose" is simply one snapshot of those
 * numbers.
 *
 * Because a pose is just numbers, two useful things follow:
 *
 *   1. Every emotion is a constant (see face_emotions.h) — no artwork needed.
 *   2. Animating between two emotions is linear interpolation between two
 *      poses, so one tween function animates every expression there is.
 *
 * The units are pixels and percent, chosen so that a human can read a pose
 * table and picture the face.
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief One complete snapshot of the face's shape.
 */
typedef struct {
    /* --- Eyes ------------------------------------------------------------- */

    /** Eye width in pixels. Wider than default = surprised/excited. */
    int16_t eye_width;

    /** Eye height in pixels. Near zero = closed (blink, sleep, happy squint). */
    int16_t eye_height;

    /** Corner rounding in pixels. High = soft and round, low = square/serious. */
    int16_t eye_corner_radius;

    /**
     * Height of a red bar drawn across the BOTTOM of each eye.
     * This is the trick that makes a "^ ^" happy squint: the eye keeps its
     * outline but only a curved sliver remains visible. 0 disables it.
     */
    int16_t eye_lower_lid;

    /** Height of a bar drawn across the TOP of each eye — a sleepy half-lid. */
    int16_t eye_upper_lid;

    /** When true the LEFT eye is forced shut regardless of eye_height (wink). */
    bool left_eye_shut;

    /** When true the RIGHT eye is forced shut regardless of eye_height. */
    bool right_eye_shut;

    /* --- Eyebrows --------------------------------------------------------- */

    /** Vertical offset of both brows. Negative lifts them (surprise). */
    int16_t brow_offset_y;

    /**
     * Tilt of each brow in pixels of drop at the INNER end (nearest the nose).
     * Positive = inner ends down = angry / determined.
     * Negative = inner ends up   = sad / worried.
     */
    int16_t brow_inner_tilt;

    /** How far apart the brows sit horizontally, as an offset from resting. */
    int16_t brow_spread;

    /**
     * Extra vertical offset applied to the LEFT brow only, in pixels.
     * Negative lifts it. This is what produces a lopsided, quizzical face —
     * one brow cocked higher than the other.
     */
    int16_t brow_asymmetry;

    /** Set false to hide the eyebrows entirely (sleeping, dizzy). */
    bool brows_visible;

    /* --- Mouth ------------------------------------------------------------ */

    /** Width of the mouth in pixels. */
    int16_t mouth_width;

    /**
     * Curvature of the closed mouth line, in pixels of rise at the ends.
     * Positive = corners up = smile. Negative = corners down = frown.
     */
    int16_t mouth_curve;

    /**
     * Height of the OPEN mouth in pixels. When greater than zero the curved
     * line is replaced by a filled rounded shape — an open, talking mouth.
     */
    int16_t mouth_open_height;

    /** Vertical offset of the whole mouth from its resting position. */
    int16_t mouth_offset_y;

    /* --- Extras ----------------------------------------------------------- */

    /** Opacity of the blush circles, 0 (hidden) to 255 (solid). */
    uint8_t blush_opacity;

    /** Where both eyes are looking, -100 (hard left/up) to +100 (right/down). */
    int16_t look_x;
    int16_t look_y;

    /** Vertical offset applied to the entire face — used for nod/bob motion. */
    int16_t face_offset_y;

    /** Horizontal offset applied to the entire face — used for head shakes. */
    int16_t face_offset_x;
} face_pose_t;

/**
 * @brief Linearly blend two poses.
 *
 * @param from      Pose at progress 0.
 * @param to        Pose at progress 255.
 * @param progress  0-255 blend position.
 * @param out       Receives the blended pose.
 */
void face_pose_blend(const face_pose_t *from, const face_pose_t *to,
                     uint8_t progress, face_pose_t *out);

#ifdef __cplusplus
}
#endif
