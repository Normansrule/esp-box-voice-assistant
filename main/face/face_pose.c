/*
 * face_pose.c — Blending between two facial expressions.
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */

#include "face_pose.h"

/**
 * @brief Interpolate one signed value.
 *
 * Uses 32-bit maths so the multiply cannot overflow for realistic pixel values.
 */
static int16_t blend_int(int16_t from, int16_t to, uint8_t progress)
{
    int32_t delta = (int32_t)to - (int32_t)from;
    return (int16_t)((int32_t)from + (delta * (int32_t)progress) / 255);
}

/**
 * @brief Interpolate one unsigned 0-255 value (opacity).
 */
static uint8_t blend_opacity(uint8_t from, uint8_t to, uint8_t progress)
{
    int32_t delta = (int32_t)to - (int32_t)from;
    int32_t value = (int32_t)from + (delta * (int32_t)progress) / 255;
    if (value < 0) {
        value = 0;
    }
    if (value > 255) {
        value = 255;
    }
    return (uint8_t)value;
}

/**
 * @brief Booleans cannot be half-true, so they flip at the halfway point.
 */
static bool blend_bool(bool from, bool to, uint8_t progress)
{
    return (progress < 128) ? from : to;
}

void face_pose_blend(const face_pose_t *from, const face_pose_t *to,
                     uint8_t progress, face_pose_t *out)
{
    if (from == NULL || to == NULL || out == NULL) {
        return;
    }

    /* Eyes */
    out->eye_width         = blend_int(from->eye_width, to->eye_width, progress);
    out->eye_height        = blend_int(from->eye_height, to->eye_height, progress);
    out->eye_corner_radius = blend_int(from->eye_corner_radius, to->eye_corner_radius, progress);
    out->eye_lower_lid     = blend_int(from->eye_lower_lid, to->eye_lower_lid, progress);
    out->eye_upper_lid     = blend_int(from->eye_upper_lid, to->eye_upper_lid, progress);
    out->left_eye_shut     = blend_bool(from->left_eye_shut, to->left_eye_shut, progress);
    out->right_eye_shut    = blend_bool(from->right_eye_shut, to->right_eye_shut, progress);

    /* Eyebrows */
    out->brow_offset_y     = blend_int(from->brow_offset_y, to->brow_offset_y, progress);
    out->brow_inner_tilt   = blend_int(from->brow_inner_tilt, to->brow_inner_tilt, progress);
    out->brow_spread       = blend_int(from->brow_spread, to->brow_spread, progress);
    out->brow_asymmetry    = blend_int(from->brow_asymmetry, to->brow_asymmetry, progress);
    out->brows_visible     = blend_bool(from->brows_visible, to->brows_visible, progress);

    /* Mouth */
    out->mouth_width       = blend_int(from->mouth_width, to->mouth_width, progress);
    out->mouth_curve       = blend_int(from->mouth_curve, to->mouth_curve, progress);
    out->mouth_open_height = blend_int(from->mouth_open_height, to->mouth_open_height, progress);
    out->mouth_offset_y    = blend_int(from->mouth_offset_y, to->mouth_offset_y, progress);

    /* Extras */
    out->blush_opacity     = blend_opacity(from->blush_opacity, to->blush_opacity, progress);
    out->look_x            = blend_int(from->look_x, to->look_x, progress);
    out->look_y            = blend_int(from->look_y, to->look_y, progress);
    out->face_offset_y     = blend_int(from->face_offset_y, to->face_offset_y, progress);
    out->face_offset_x     = blend_int(from->face_offset_x, to->face_offset_x, progress);
}
