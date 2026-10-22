/*
 * face_emotions.c — The expression catalogue and the idle antic scripts.
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 *
 * This file contains no logic worth speaking of. It is a table of numbers that
 * describes what each mood looks like, plus a table of little performances the
 * buddy puts on when it is bored or poked.
 *
 * Want a new mood? Add an entry to face_emotion_t in the header and a row here.
 * Want a new antic? Add a step array and list it in s_antics.
 */

#include <string.h>
#include <strings.h>

#include "buddy_config.h"
#include "face_emotions.h"

/*
 * Shorthand for the resting dimensions, so the table below stays readable.
 *
 * These are the ORIGINAL design sizes, not the scaled ones. Every pixel field
 * in the table is written at design size and wrapped in FACE_SCALED(), so that
 * changing FACE_SCALE_PERCENT in buddy_config.h resizes the whole face —
 * including every per-expression tweak — in proportion. Mixing a scaled base
 * with an unscaled offset is exactly the bug that makes a shrunken face look
 * subtly wrong, so the wrapping is applied uniformly rather than by eye.
 */
#define EYE_W   60
#define EYE_H   74
#define EYE_R   26
#define MOUTH_W 96

/** Shorthand for FACE_SCALED(), purely to keep the rows below one line each. */
#define S(px)   FACE_SCALED(px)

/* ------------------------------------------------------------------------ */
/* The expression table                                                      */
/* ------------------------------------------------------------------------ */

/*
 * Reading guide for each row:
 *
 *   eye_width / eye_height / eye_corner_radius
 *       The red rounded rectangle that is an eye. Bigger and rounder reads as
 *       younger and more excited; smaller and squarer reads as sceptical.
 *
 *   eye_lower_lid / eye_upper_lid
 *       How deeply a background-coloured crescent bites into the eye from the
 *       bottom or the top. A big lower lid leaves a "^" arc — the classic
 *       delighted squint. A big upper lid leaves a "v" — heavy and sleepy.
 *
 *   brow_offset_y      negative = brows lifted
 *   brow_inner_tilt    positive = inner ends down (cross), negative = up (sad)
 *   brow_asymmetry     negative = left brow lifted above the right (quizzical)
 *
 *   mouth_curve        positive = smile, negative = frown
 *   mouth_open_height  greater than zero swaps the smile line for an open mouth
 */
static const face_pose_t s_emotion_poses[FACE_EMOTION_COUNT] = {

    [FACE_EMOTION_NEUTRAL] = {
        .eye_width = S(EYE_W),        .eye_height = S(EYE_H),       .eye_corner_radius = S(EYE_R),
        .eye_lower_lid = 0,           .eye_upper_lid = 0,
        .left_eye_shut = false,       .right_eye_shut = false,
        .brow_offset_y = 0,           .brow_inner_tilt = 0,         .brow_spread = 0,
        .brow_asymmetry = 0,          .brows_visible = true,
        .mouth_width = S(MOUTH_W),    .mouth_curve = S(12),         .mouth_open_height = 0,
        .mouth_offset_y = 0,
        .blush_opacity = 0,           .look_x = 0,                  .look_y = 0,
        .face_offset_x = 0,           .face_offset_y = 0,
    },

    [FACE_EMOTION_HAPPY] = {
        .eye_width = S(EYE_W+6),      .eye_height = S(EYE_H),       .eye_corner_radius = S(EYE_R+6),
        .eye_lower_lid = S(48),       .eye_upper_lid = 0,
        .left_eye_shut = false,       .right_eye_shut = false,
        .brow_offset_y = S(-7),       .brow_inner_tilt = S(-3),     .brow_spread = 0,
        .brow_asymmetry = 0,          .brows_visible = true,
        .mouth_width = S(MOUTH_W+16), .mouth_curve = S(26),         .mouth_open_height = 0,
        .mouth_offset_y = S(-2),
        .blush_opacity = 170,         .look_x = 0,                  .look_y = 0,
        .face_offset_x = 0,           .face_offset_y = 0,
    },

    [FACE_EMOTION_EXCITED] = {
        .eye_width = S(EYE_W+12),     .eye_height = S(EYE_H+10),    .eye_corner_radius = S(EYE_R+10),
        .eye_lower_lid = 0,           .eye_upper_lid = 0,
        .left_eye_shut = false,       .right_eye_shut = false,
        .brow_offset_y = S(-14),      .brow_inner_tilt = S(-5),     .brow_spread = S(2),
        .brow_asymmetry = 0,          .brows_visible = true,
        .mouth_width = S(MOUTH_W+8),  .mouth_curve = S(18),         .mouth_open_height = S(36),
        .mouth_offset_y = 0,
        .blush_opacity = 120,         .look_x = 0,                  .look_y = -6,
        .face_offset_x = 0,           .face_offset_y = S(-3),
    },

    [FACE_EMOTION_CURIOUS] = {
        .eye_width = S(EYE_W+2),      .eye_height = S(EYE_H+4),     .eye_corner_radius = S(EYE_R+2),
        .eye_lower_lid = 0,           .eye_upper_lid = 0,
        .left_eye_shut = false,       .right_eye_shut = false,
        .brow_offset_y = S(-8),       .brow_inner_tilt = S(-4),     .brow_spread = S(3),
        .brow_asymmetry = S(-11),     .brows_visible = true,
        .mouth_width = S(MOUTH_W-26), .mouth_curve = S(8),          .mouth_open_height = 0,
        .mouth_offset_y = 0,
        .blush_opacity = 0,           .look_x = 45,                 .look_y = -18,
        .face_offset_x = 0,           .face_offset_y = 0,
    },

    [FACE_EMOTION_SURPRISED] = {
        .eye_width = S(EYE_W+18),     .eye_height = S(EYE_H+16),    .eye_corner_radius = S(EYE_R+16),
        .eye_lower_lid = 0,           .eye_upper_lid = 0,
        .left_eye_shut = false,       .right_eye_shut = false,
        .brow_offset_y = S(-18),      .brow_inner_tilt = S(-7),     .brow_spread = S(4),
        .brow_asymmetry = 0,          .brows_visible = true,
        .mouth_width = S(MOUTH_W-50), .mouth_curve = 0,             .mouth_open_height = S(42),
        .mouth_offset_y = S(2),
        .blush_opacity = 0,           .look_x = 0,                  .look_y = 0,
        .face_offset_x = 0,           .face_offset_y = S(-4),
    },

    [FACE_EMOTION_SAD] = {
        .eye_width = S(EYE_W-2),      .eye_height = S(EYE_H-8),     .eye_corner_radius = S(EYE_R),
        .eye_lower_lid = 0,           .eye_upper_lid = S(14),
        .left_eye_shut = false,       .right_eye_shut = false,
        .brow_offset_y = S(4),        .brow_inner_tilt = S(-13),    .brow_spread = S(-2),
        .brow_asymmetry = 0,          .brows_visible = true,
        .mouth_width = S(MOUTH_W-12), .mouth_curve = S(-20),        .mouth_open_height = 0,
        .mouth_offset_y = S(4),
        .blush_opacity = 0,           .look_x = 0,                  .look_y = 28,
        .face_offset_x = 0,           .face_offset_y = S(3),
    },

    [FACE_EMOTION_ANNOYED] = {
        .eye_width = S(EYE_W),        .eye_height = S(EYE_H-20),    .eye_corner_radius = S(EYE_R-8),
        .eye_lower_lid = 0,           .eye_upper_lid = S(18),
        .left_eye_shut = false,       .right_eye_shut = false,
        .brow_offset_y = S(9),        .brow_inner_tilt = S(13),     .brow_spread = S(-3),
        .brow_asymmetry = 0,          .brows_visible = true,
        .mouth_width = S(MOUTH_W-26), .mouth_curve = S(-8),         .mouth_open_height = 0,
        .mouth_offset_y = 0,
        .blush_opacity = 0,           .look_x = -20,                .look_y = 0,
        .face_offset_x = 0,           .face_offset_y = 0,
    },

    [FACE_EMOTION_SLEEPY] = {
        .eye_width = S(EYE_W),        .eye_height = S(EYE_H),       .eye_corner_radius = S(EYE_R),
        .eye_lower_lid = 0,           .eye_upper_lid = S(48),
        .left_eye_shut = false,       .right_eye_shut = false,
        .brow_offset_y = S(7),        .brow_inner_tilt = S(-3),     .brow_spread = 0,
        .brow_asymmetry = 0,          .brows_visible = true,
        .mouth_width = S(MOUTH_W-48), .mouth_curve = S(6),          .mouth_open_height = 0,
        .mouth_offset_y = S(2),
        .blush_opacity = 0,           .look_x = 0,                  .look_y = 30,
        .face_offset_x = 0,           .face_offset_y = S(2),
    },

    [FACE_EMOTION_YAWN] = {
        .eye_width = S(EYE_W),        .eye_height = S(EYE_H),       .eye_corner_radius = S(EYE_R),
        .eye_lower_lid = S(52),       .eye_upper_lid = S(12),
        .left_eye_shut = false,       .right_eye_shut = false,
        .brow_offset_y = S(-6),       .brow_inner_tilt = S(-6),     .brow_spread = 0,
        .brow_asymmetry = 0,          .brows_visible = true,
        .mouth_width = S(MOUTH_W-34), .mouth_curve = 0,             .mouth_open_height = S(44),
        .mouth_offset_y = S(4),
        .blush_opacity = 60,          .look_x = 0,                  .look_y = 10,
        .face_offset_x = 0,           .face_offset_y = S(2),
    },

    [FACE_EMOTION_ASLEEP] = {
        .eye_width = S(EYE_W),        .eye_height = S(EYE_H),       .eye_corner_radius = S(EYE_R),
        .eye_lower_lid = 0,           .eye_upper_lid = 0,
        .left_eye_shut = true,        .right_eye_shut = true,
        .brow_offset_y = S(6),        .brow_inner_tilt = 0,         .brow_spread = 0,
        .brow_asymmetry = 0,          .brows_visible = false,
        .mouth_width = S(MOUTH_W-52), .mouth_curve = S(8),          .mouth_open_height = 0,
        .mouth_offset_y = S(2),
        .blush_opacity = 0,           .look_x = 0,                  .look_y = 0,
        .face_offset_x = 0,           .face_offset_y = S(4),
    },

    [FACE_EMOTION_WINK] = {
        .eye_width = S(EYE_W+6),      .eye_height = S(EYE_H),       .eye_corner_radius = S(EYE_R+6),
        .eye_lower_lid = 0,           .eye_upper_lid = 0,
        .left_eye_shut = true,        .right_eye_shut = false,
        .brow_offset_y = S(-6),       .brow_inner_tilt = S(-2),     .brow_spread = 0,
        .brow_asymmetry = S(-8),      .brows_visible = true,
        .mouth_width = S(MOUTH_W+12), .mouth_curve = S(24),         .mouth_open_height = 0,
        .mouth_offset_y = S(-2),
        .blush_opacity = 140,         .look_x = 12,                 .look_y = 0,
        .face_offset_x = 0,           .face_offset_y = 0,
    },

    [FACE_EMOTION_LOVE] = {
        .eye_width = S(EYE_W+10),     .eye_height = S(EYE_H),       .eye_corner_radius = S(EYE_R+8),
        .eye_lower_lid = S(46),       .eye_upper_lid = 0,
        .left_eye_shut = false,       .right_eye_shut = false,
        .brow_offset_y = S(-9),       .brow_inner_tilt = S(-5),     .brow_spread = 0,
        .brow_asymmetry = 0,          .brows_visible = true,
        .mouth_width = S(MOUTH_W+4),  .mouth_curve = S(24),         .mouth_open_height = S(18),
        .mouth_offset_y = 0,
        .blush_opacity = 225,         .look_x = 0,                  .look_y = -4,
        .face_offset_x = 0,           .face_offset_y = S(-2),
    },

    [FACE_EMOTION_CONFUSED] = {
        .eye_width = S(EYE_W),        .eye_height = S(EYE_H),       .eye_corner_radius = S(EYE_R),
        .eye_lower_lid = 0,           .eye_upper_lid = S(6),
        .left_eye_shut = false,       .right_eye_shut = false,
        .brow_offset_y = S(-4),       .brow_inner_tilt = S(6),      .brow_spread = 0,
        .brow_asymmetry = S(-14),     .brows_visible = true,
        .mouth_width = S(MOUTH_W-32), .mouth_curve = S(-7),         .mouth_open_height = 0,
        .mouth_offset_y = S(3),
        .blush_opacity = 0,           .look_x = -38,                .look_y = -12,
        .face_offset_x = 0,           .face_offset_y = 0,
    },

    [FACE_EMOTION_DIZZY] = {
        .eye_width = S(34),           .eye_height = S(34),          .eye_corner_radius = S(17),
        .eye_lower_lid = 0,           .eye_upper_lid = 0,
        .left_eye_shut = false,       .right_eye_shut = false,
        .brow_offset_y = 0,           .brow_inner_tilt = 0,         .brow_spread = 0,
        .brow_asymmetry = 0,          .brows_visible = false,
        .mouth_width = S(MOUTH_W-34), .mouth_curve = S(-14),        .mouth_open_height = S(16),
        .mouth_offset_y = S(2),
        .blush_opacity = 90,          .look_x = 0,                  .look_y = 0,
        .face_offset_x = 0,           .face_offset_y = 0,
    },

    [FACE_EMOTION_MISCHIEF] = {
        .eye_width = S(EYE_W-4),      .eye_height = S(EYE_H-16),    .eye_corner_radius = S(EYE_R-4),
        .eye_lower_lid = 0,           .eye_upper_lid = S(20),
        .left_eye_shut = false,       .right_eye_shut = false,
        .brow_offset_y = S(-4),       .brow_inner_tilt = S(5),      .brow_spread = 0,
        .brow_asymmetry = S(-10),     .brows_visible = true,
        .mouth_width = S(MOUTH_W-20), .mouth_curve = S(16),         .mouth_open_height = 0,
        .mouth_offset_y = 0,
        .blush_opacity = 40,          .look_x = 30,                 .look_y = 0,
        .face_offset_x = 0,           .face_offset_y = 0,
    },

    [FACE_EMOTION_PROUD] = {
        .eye_width = S(EYE_W+4),      .eye_height = S(EYE_H),       .eye_corner_radius = S(EYE_R+6),
        .eye_lower_lid = S(46),       .eye_upper_lid = 0,
        .left_eye_shut = false,       .right_eye_shut = false,
        .brow_offset_y = S(-10),      .brow_inner_tilt = S(-4),     .brow_spread = S(2),
        .brow_asymmetry = 0,          .brows_visible = true,
        .mouth_width = S(MOUTH_W+10), .mouth_curve = S(22),         .mouth_open_height = 0,
        .mouth_offset_y = S(-2),
        .blush_opacity = 90,          .look_x = 0,                  .look_y = -14,
        .face_offset_x = 0,           .face_offset_y = S(-3),
    },

    [FACE_EMOTION_BORED] = {
        .eye_width = S(EYE_W),        .eye_height = S(EYE_H-14),    .eye_corner_radius = S(EYE_R-4),
        .eye_lower_lid = 0,           .eye_upper_lid = S(24),
        .left_eye_shut = false,       .right_eye_shut = false,
        .brow_offset_y = S(6),        .brow_inner_tilt = S(2),      .brow_spread = 0,
        .brow_asymmetry = 0,          .brows_visible = true,
        .mouth_width = S(MOUTH_W-18), .mouth_curve = S(-4),         .mouth_open_height = 0,
        .mouth_offset_y = S(2),
        .blush_opacity = 0,           .look_x = -55,                .look_y = 12,
        .face_offset_x = 0,           .face_offset_y = 0,
    },

    /*
     * Lids at half mast plus a cocked brow is the whole trick here — the mouth
     * only has to lean a little for the face to read as pleased with itself.
     */
    [FACE_EMOTION_SMUG] = {
        .eye_width = S(EYE_W),        .eye_height = S(EYE_H-8),     .eye_corner_radius = S(EYE_R),
        .eye_lower_lid = 0,           .eye_upper_lid = S(22),
        .left_eye_shut = false,       .right_eye_shut = false,
        .brow_offset_y = S(-2),       .brow_inner_tilt = S(3),      .brow_spread = 0,
        .brow_asymmetry = S(-11),     .brows_visible = true,
        .mouth_width = S(MOUTH_W-14), .mouth_curve = S(9),          .mouth_open_height = 0,
        .mouth_offset_y = S(2),
        .blush_opacity = 0,           .look_x = 26,                 .look_y = -8,
        .face_offset_x = 0,           .face_offset_y = S(-2),
    },

    /*
     * Deliberately louder than SURPRISED: wider eyes, a taller open mouth and
     * brows pushed to the clamp. Having both lets a small "oh!" and a genuine
     * "WHAT?!" read differently rather than sharing one face.
     */
    [FACE_EMOTION_SHOCKED] = {
        .eye_width = S(EYE_W+16),     .eye_height = S(EYE_H+16),    .eye_corner_radius = S(EYE_R+10),
        .eye_lower_lid = 0,           .eye_upper_lid = 0,
        .left_eye_shut = false,       .right_eye_shut = false,
        .brow_offset_y = S(-20),      .brow_inner_tilt = S(-5),     .brow_spread = S(4),
        .brow_asymmetry = 0,          .brows_visible = true,
        .mouth_width = S(MOUTH_W-30), .mouth_curve = 0,             .mouth_open_height = S(38),
        .mouth_offset_y = S(4),
        .blush_opacity = 0,           .look_x = 0,                  .look_y = 0,
        .face_offset_x = 0,           .face_offset_y = S(-4),
    },

    /* Brows down and IN, eyes narrowed, mouth a flat line. Nothing soft. */
    [FACE_EMOTION_DETERMINED] = {
        .eye_width = S(EYE_W+2),      .eye_height = S(EYE_H-18),    .eye_corner_radius = S(EYE_R-10),
        .eye_lower_lid = 0,           .eye_upper_lid = S(14),
        .left_eye_shut = false,       .right_eye_shut = false,
        .brow_offset_y = S(4),        .brow_inner_tilt = S(11),     .brow_spread = S(-5),
        .brow_asymmetry = 0,          .brows_visible = true,
        .mouth_width = S(MOUTH_W-6),  .mouth_curve = S(1),          .mouth_open_height = 0,
        .mouth_offset_y = 0,
        .blush_opacity = 0,           .look_x = 0,                  .look_y = -6,
        .face_offset_x = 0,           .face_offset_y = 0,
    },

    /* Happy, but with the mouth open and the whole face lifted mid-bounce. */
    [FACE_EMOTION_GIGGLY] = {
        .eye_width = S(EYE_W+6),      .eye_height = S(EYE_H),       .eye_corner_radius = S(EYE_R+8),
        .eye_lower_lid = S(52),       .eye_upper_lid = 0,
        .left_eye_shut = false,       .right_eye_shut = false,
        .brow_offset_y = S(-9),       .brow_inner_tilt = S(-4),     .brow_spread = 0,
        .brow_asymmetry = 0,          .brows_visible = true,
        .mouth_width = S(MOUTH_W-4),  .mouth_curve = S(16),         .mouth_open_height = S(26),
        .mouth_offset_y = S(2),
        .blush_opacity = 235,         .look_x = 0,                  .look_y = 4,
        .face_offset_x = 0,           .face_offset_y = S(-6),
    },

    /*
     * The pose struct has no per-eye lid, so "one eye narrowed" is out of
     * reach. Uneven BROWS over two heavily lidded eyes and a hard side-eye get
     * to the same place, and it stays distinct from MISCHIEF because the mouth
     * is flat rather than smirking.
     */
    [FACE_EMOTION_SUSPICIOUS] = {
        .eye_width = S(EYE_W-2),      .eye_height = S(EYE_H-12),    .eye_corner_radius = S(EYE_R-6),
        .eye_lower_lid = 0,           .eye_upper_lid = S(26),
        .left_eye_shut = false,       .right_eye_shut = false,
        .brow_offset_y = S(2),        .brow_inner_tilt = S(6),      .brow_spread = S(-3),
        .brow_asymmetry = S(-14),     .brows_visible = true,
        .mouth_width = S(MOUTH_W-22), .mouth_curve = S(-2),         .mouth_open_height = 0,
        .mouth_offset_y = S(2),
        .blush_opacity = 0,           .look_x = -62,                .look_y = 0,
        .face_offset_x = 0,           .face_offset_y = 0,
    },

    /* Eyes closed in soft arcs, brows released upward, a small quiet smile. */
    [FACE_EMOTION_RELIEVED] = {
        .eye_width = S(EYE_W+2),      .eye_height = S(EYE_H),       .eye_corner_radius = S(EYE_R+4),
        .eye_lower_lid = S(44),       .eye_upper_lid = S(12),
        .left_eye_shut = false,       .right_eye_shut = false,
        .brow_offset_y = S(-8),       .brow_inner_tilt = S(-6),     .brow_spread = 0,
        .brow_asymmetry = 0,          .brows_visible = true,
        .mouth_width = S(MOUTH_W-20), .mouth_curve = S(10),         .mouth_open_height = 0,
        .mouth_offset_y = S(2),
        .blush_opacity = 90,          .look_x = 0,                  .look_y = 0,
        .face_offset_x = 0,           .face_offset_y = S(2),
    },

    /* Wonder rather than shock: the eyes go round and the mouth goes happy. */
    [FACE_EMOTION_AMAZED] = {
        .eye_width = S(EYE_W+14),     .eye_height = S(EYE_H+12),    .eye_corner_radius = S(EYE_R+14),
        .eye_lower_lid = 0,           .eye_upper_lid = 0,
        .left_eye_shut = false,       .right_eye_shut = false,
        .brow_offset_y = S(-16),      .brow_inner_tilt = S(-3),     .brow_spread = S(3),
        .brow_asymmetry = 0,          .brows_visible = true,
        .mouth_width = S(MOUTH_W-8),  .mouth_curve = S(14),         .mouth_open_height = S(22),
        .mouth_offset_y = S(2),
        .blush_opacity = 150,         .look_x = 0,                  .look_y = -6,
        .face_offset_x = 0,           .face_offset_y = S(-3),
    },

    /* Wide eyes, inner brows up, a tiny mouth, and caught mid-fidget. */
    [FACE_EMOTION_NERVOUS] = {
        .eye_width = S(EYE_W+8),      .eye_height = S(EYE_H+6),     .eye_corner_radius = S(EYE_R+4),
        .eye_lower_lid = 0,           .eye_upper_lid = S(4),
        .left_eye_shut = false,       .right_eye_shut = false,
        .brow_offset_y = S(-12),      .brow_inner_tilt = S(-12),    .brow_spread = S(4),
        .brow_asymmetry = S(-4),      .brows_visible = true,
        .mouth_width = S(MOUTH_W-38), .mouth_curve = S(-3),         .mouth_open_height = 0,
        .mouth_offset_y = S(4),
        .blush_opacity = 120,         .look_x = 34,                 .look_y = 10,
        .face_offset_x = S(3),        .face_offset_y = 0,
    },

    /* Bigger eyes than anything else, and brows begging. Impossible to refuse. */
    [FACE_EMOTION_PLEADING] = {
        .eye_width = S(EYE_W+18),     .eye_height = S(EYE_H+18),    .eye_corner_radius = S(EYE_R+16),
        .eye_lower_lid = 0,           .eye_upper_lid = 0,
        .left_eye_shut = false,       .right_eye_shut = false,
        .brow_offset_y = S(-14),      .brow_inner_tilt = S(-14),    .brow_spread = S(3),
        .brow_asymmetry = 0,          .brows_visible = true,
        .mouth_width = S(MOUTH_W-42), .mouth_curve = S(-6),         .mouth_open_height = 0,
        .mouth_offset_y = S(5),
        .blush_opacity = 140,         .look_x = 0,                  .look_y = -12,
        .face_offset_x = 0,           .face_offset_y = S(3),
    },

    /* PROUD keeps its mouth shut; this one cannot contain itself. */
    [FACE_EMOTION_TRIUMPHANT] = {
        .eye_width = S(EYE_W+8),      .eye_height = S(EYE_H),       .eye_corner_radius = S(EYE_R+8),
        .eye_lower_lid = S(54),       .eye_upper_lid = 0,
        .left_eye_shut = false,       .right_eye_shut = false,
        .brow_offset_y = S(-13),      .brow_inner_tilt = S(-5),     .brow_spread = S(2),
        .brow_asymmetry = 0,          .brows_visible = true,
        .mouth_width = S(MOUTH_W+2),  .mouth_curve = S(18),         .mouth_open_height = S(34),
        .mouth_offset_y = 0,
        .blush_opacity = 190,         .look_x = 0,                  .look_y = -8,
        .face_offset_x = 0,           .face_offset_y = S(-7),
    },

    /*
     * Not bored and not cross — just entirely unmoved. BORED looks away and
     * ANNOYED pulls its brows down; this one holds your gaze with one brow up
     * and a mouth that does nothing at all, which is the whole joke.
     */
    [FACE_EMOTION_UNIMPRESSED] = {
        .eye_width = S(EYE_W+2),      .eye_height = S(EYE_H-16),    .eye_corner_radius = S(EYE_R-8),
        .eye_lower_lid = 0,           .eye_upper_lid = S(20),
        .left_eye_shut = false,       .right_eye_shut = false,
        .brow_offset_y = S(-3),       .brow_inner_tilt = 0,         .brow_spread = 0,
        .brow_asymmetry = S(-13),     .brows_visible = true,
        .mouth_width = S(MOUTH_W-10), .mouth_curve = 0,             .mouth_open_height = 0,
        .mouth_offset_y = 0,
        .blush_opacity = 0,           .look_x = 0,                  .look_y = 0,
        .face_offset_x = 0,           .face_offset_y = 0,
    },

    /* Everything pulled in towards the middle, as if it just bit a lemon. */
    [FACE_EMOTION_SCRUNCH] = {
        .eye_width = S(EYE_W-4),      .eye_height = S(EYE_H),       .eye_corner_radius = S(EYE_R),
        .eye_lower_lid = S(34),       .eye_upper_lid = S(30),
        .left_eye_shut = false,       .right_eye_shut = false,
        .brow_offset_y = S(9),        .brow_inner_tilt = S(13),     .brow_spread = S(-8),
        .brow_asymmetry = 0,          .brows_visible = true,
        .mouth_width = S(MOUTH_W-44), .mouth_curve = S(-5),         .mouth_open_height = S(14),
        .mouth_offset_y = S(-3),
        .blush_opacity = 80,          .look_x = 0,                  .look_y = 0,
        .face_offset_x = 0,           .face_offset_y = S(2),
    },

    /* Turned away and down, mouth small and pleased, cheeks doing the talking. */
    [FACE_EMOTION_SHY] = {
        .eye_width = S(EYE_W),        .eye_height = S(EYE_H-6),     .eye_corner_radius = S(EYE_R+4),
        .eye_lower_lid = S(26),       .eye_upper_lid = S(10),
        .left_eye_shut = false,       .right_eye_shut = false,
        .brow_offset_y = S(-6),       .brow_inner_tilt = S(-8),     .brow_spread = 0,
        .brow_asymmetry = S(-5),      .brows_visible = true,
        .mouth_width = S(MOUTH_W-34), .mouth_curve = S(8),          .mouth_open_height = 0,
        .mouth_offset_y = S(3),
        .blush_opacity = 255,         .look_x = -48,                .look_y = 46,
        .face_offset_x = S(-3),       .face_offset_y = S(2),
    },

    /*
     * Louder still than SHOCKED. The difference is the mouth — a full open
     * rectangle rather than a small round one — plus the sideways offset, which
     * reads as the whole head flinching.
     */
    [FACE_EMOTION_PANIC] = {
        .eye_width = S(EYE_W+20),     .eye_height = S(EYE_H+20),    .eye_corner_radius = S(EYE_R+12),
        .eye_lower_lid = 0,           .eye_upper_lid = 0,
        .left_eye_shut = false,       .right_eye_shut = false,
        .brow_offset_y = S(-22),      .brow_inner_tilt = S(-8),     .brow_spread = S(6),
        .brow_asymmetry = S(-6),      .brows_visible = true,
        .mouth_width = S(MOUTH_W-8),  .mouth_curve = S(-8),         .mouth_open_height = S(42),
        .mouth_offset_y = S(4),
        .blush_opacity = 0,           .look_x = 22,                 .look_y = -10,
        .face_offset_x = S(4),        .face_offset_y = S(-5),
    },

    /* Brows fighting each other, lids low, mouth curled away from whatever it is. */
    [FACE_EMOTION_DISGUSTED] = {
        .eye_width = S(EYE_W-2),      .eye_height = S(EYE_H-10),    .eye_corner_radius = S(EYE_R-4),
        .eye_lower_lid = 0,           .eye_upper_lid = S(22),
        .left_eye_shut = false,       .right_eye_shut = false,
        .brow_offset_y = S(1),        .brow_inner_tilt = S(9),      .brow_spread = S(-4),
        .brow_asymmetry = S(-16),     .brows_visible = true,
        .mouth_width = S(MOUTH_W-26), .mouth_curve = S(-13),        .mouth_open_height = 0,
        .mouth_offset_y = S(3),
        .blush_opacity = 0,           .look_x = -38,                .look_y = 16,
        .face_offset_x = S(-2),       .face_offset_y = 0,
    },

    /*
     * Worn while music plays. A narrow, tall opening is what separates singing
     * from talking — the lip-sync animator overwrites mouth_open_height every
     * frame anyway, so what this row really contributes is the blissful eyes
     * and the narrow mouth WIDTH the opening is drawn inside.
     */
    [FACE_EMOTION_SINGING] = {
        .eye_width = S(EYE_W+4),      .eye_height = S(EYE_H),       .eye_corner_radius = S(EYE_R+8),
        .eye_lower_lid = S(50),       .eye_upper_lid = 0,
        .left_eye_shut = false,       .right_eye_shut = false,
        .brow_offset_y = S(-11),      .brow_inner_tilt = S(-6),     .brow_spread = 0,
        .brow_asymmetry = 0,          .brows_visible = true,
        .mouth_width = S(MOUTH_W-46), .mouth_curve = S(6),          .mouth_open_height = S(30),
        .mouth_offset_y = S(2),
        .blush_opacity = 170,         .look_x = 0,                  .look_y = -6,
        .face_offset_x = 0,           .face_offset_y = S(-4),
    },

    /* ---------------------------------------------------------------------
     * Added in v3.5.
     *
     * The twelve below are all built from the same handful of levers as
     * everything above — lid heights, brow tilt, gaze, mouth curve — and the
     * reason they read as distinct faces is that each one commits to a
     * COMBINATION rather than turning one dial. SCEPTICAL is not "surprised
     * with one brow"; it is a raised brow AND low lids AND a mouth pulled to
     * one side, and dropping any of the three collapses it into a face that
     * already existed.
     * ------------------------------------------------------------------- */

    /* Eyes at their absolute limit, blush high: delight at something wonderful. */
    [FACE_EMOTION_STARRY] = {
        .eye_width = S(EYE_W+22),     .eye_height = S(EYE_H+22),    .eye_corner_radius = S(EYE_R+16),
        .eye_lower_lid = 0,           .eye_upper_lid = 0,
        .left_eye_shut = false,       .right_eye_shut = false,
        .brow_offset_y = S(-14),      .brow_inner_tilt = S(-4),     .brow_spread = S(3),
        .brow_asymmetry = 0,          .brows_visible = true,
        .mouth_width = S(MOUTH_W-18), .mouth_curve = S(16),         .mouth_open_height = S(16),
        .mouth_offset_y = S(2),
        .blush_opacity = 210,         .look_x = 0,                  .look_y = -10,
        .face_offset_x = 0,           .face_offset_y = S(-3),
    },

    /* The "go on then, prove it" face. Brow up, lids down, mouth off-centre. */
    [FACE_EMOTION_SCEPTICAL] = {
        .eye_width = S(EYE_W),        .eye_height = S(EYE_H-14),    .eye_corner_radius = S(EYE_R-4),
        .eye_lower_lid = 0,           .eye_upper_lid = S(26),
        .left_eye_shut = false,       .right_eye_shut = false,
        .brow_offset_y = S(-3),       .brow_inner_tilt = S(2),      .brow_spread = 0,
        .brow_asymmetry = S(-18),     .brows_visible = true,
        .mouth_width = S(MOUTH_W-30), .mouth_curve = S(-3),         .mouth_open_height = 0,
        .mouth_offset_y = S(3),
        .blush_opacity = 0,           .look_x = 22,                 .look_y = 0,
        .face_offset_x = S(2),        .face_offset_y = 0,
    },

    /* Caught out and not minding much: gaze away, small smile, blush. */
    [FACE_EMOTION_SHEEPISH] = {
        .eye_width = S(EYE_W-4),      .eye_height = S(EYE_H-18),    .eye_corner_radius = S(EYE_R),
        .eye_lower_lid = S(10),       .eye_upper_lid = S(14),
        .left_eye_shut = false,       .right_eye_shut = false,
        .brow_offset_y = S(-6),       .brow_inner_tilt = S(-6),     .brow_spread = 0,
        .brow_asymmetry = S(-5),      .brows_visible = true,
        .mouth_width = S(MOUTH_W-34), .mouth_curve = S(7),          .mouth_open_height = 0,
        .mouth_offset_y = S(2),
        .blush_opacity = 195,         .look_x = -34,                .look_y = 14,
        .face_offset_x = S(-2),       .face_offset_y = 0,
    },

    /* Both brows lifted TOGETHER — the difference between hope and surprise. */
    [FACE_EMOTION_HOPEFUL] = {
        .eye_width = S(EYE_W+8),      .eye_height = S(EYE_H+8),     .eye_corner_radius = S(EYE_R+8),
        .eye_lower_lid = 0,           .eye_upper_lid = 0,
        .left_eye_shut = false,       .right_eye_shut = false,
        .brow_offset_y = S(-13),      .brow_inner_tilt = S(-9),     .brow_spread = S(-2),
        .brow_asymmetry = 0,          .brows_visible = true,
        .mouth_width = S(MOUTH_W-26), .mouth_curve = S(8),          .mouth_open_height = 0,
        .mouth_offset_y = 0,
        .blush_opacity = 90,          .look_x = 0,                  .look_y = -22,
        .face_offset_x = 0,           .face_offset_y = S(-2),
    },

    /* Working something out. Distinct from THINKING, which looks up; this looks past you. */
    [FACE_EMOTION_THOUGHTFUL] = {
        .eye_width = S(EYE_W-2),      .eye_height = S(EYE_H-16),    .eye_corner_radius = S(EYE_R),
        .eye_lower_lid = 0,           .eye_upper_lid = S(20),
        .left_eye_shut = false,       .right_eye_shut = false,
        .brow_offset_y = S(2),        .brow_inner_tilt = S(4),      .brow_spread = S(-3),
        .brow_asymmetry = S(-7),      .brows_visible = true,
        .mouth_width = S(MOUTH_W-38), .mouth_curve = 0,             .mouth_open_height = 0,
        .mouth_offset_y = S(3),
        .blush_opacity = 0,           .look_x = -30,                .look_y = -6,
        .face_offset_x = 0,           .face_offset_y = 0,
    },

    /* One eye squeezed shut mid-grin: a wink that is having a better time. */
    [FACE_EMOTION_CHEEKY] = {
        .eye_width = S(EYE_W),        .eye_height = S(EYE_H-6),     .eye_corner_radius = S(EYE_R+4),
        .eye_lower_lid = S(20),       .eye_upper_lid = 0,
        .left_eye_shut = false,       .right_eye_shut = true,
        .brow_offset_y = S(-8),       .brow_inner_tilt = S(-2),     .brow_spread = 0,
        .brow_asymmetry = S(-13),     .brows_visible = true,
        .mouth_width = S(MOUTH_W+4),  .mouth_curve = S(15),         .mouth_open_height = S(12),
        .mouth_offset_y = S(1),
        .blush_opacity = 150,         .look_x = 16,                 .look_y = -4,
        .face_offset_x = S(2),        .face_offset_y = S(-2),
    },

    /* Startled rather than shocked: the mouth stays small and tight. */
    [FACE_EMOTION_ALARMED] = {
        .eye_width = S(EYE_W+14),     .eye_height = S(EYE_H+18),    .eye_corner_radius = S(EYE_R+6),
        .eye_lower_lid = 0,           .eye_upper_lid = 0,
        .left_eye_shut = false,       .right_eye_shut = false,
        .brow_offset_y = S(-18),      .brow_inner_tilt = S(-3),     .brow_spread = S(4),
        .brow_asymmetry = S(-4),      .brows_visible = true,
        .mouth_width = S(MOUTH_W-52), .mouth_curve = S(-6),         .mouth_open_height = S(10),
        .mouth_offset_y = S(4),
        .blush_opacity = 0,           .look_x = -10,                .look_y = -14,
        .face_offset_x = S(-2),       .face_offset_y = S(-4),
    },

    /* Contentment. Softer than HAPPY, and going nowhere. */
    [FACE_EMOTION_COSY] = {
        .eye_width = S(EYE_W-2),      .eye_height = S(EYE_H-30),    .eye_corner_radius = S(EYE_R+10),
        .eye_lower_lid = S(30),       .eye_upper_lid = 0,
        .left_eye_shut = false,       .right_eye_shut = false,
        .brow_offset_y = S(-4),       .brow_inner_tilt = S(-3),     .brow_spread = 0,
        .brow_asymmetry = 0,          .brows_visible = true,
        .mouth_width = S(MOUTH_W-32), .mouth_curve = S(11),         .mouth_open_height = 0,
        .mouth_offset_y = S(2),
        .blush_opacity = 175,         .look_x = 0,                  .look_y = 6,
        .face_offset_x = 0,           .face_offset_y = S(2),
    },

    /* Somewhere else entirely. Half-lidded and looking up past your shoulder. */
    [FACE_EMOTION_DREAMY] = {
        .eye_width = S(EYE_W+4),      .eye_height = S(EYE_H-20),    .eye_corner_radius = S(EYE_R+8),
        .eye_lower_lid = 0,           .eye_upper_lid = S(24),
        .left_eye_shut = false,       .right_eye_shut = false,
        .brow_offset_y = S(-7),       .brow_inner_tilt = S(-5),     .brow_spread = S(2),
        .brow_asymmetry = S(-3),      .brows_visible = true,
        .mouth_width = S(MOUTH_W-30), .mouth_curve = S(6),          .mouth_open_height = 0,
        .mouth_offset_y = S(1),
        .blush_opacity = 120,         .look_x = 30,                 .look_y = -26,
        .face_offset_x = 0,           .face_offset_y = S(-1),
    },

    /* Locked on. Narrow, level, and pointed straight at you. */
    [FACE_EMOTION_FOCUSED] = {
        .eye_width = S(EYE_W-6),      .eye_height = S(EYE_H-26),    .eye_corner_radius = S(EYE_R-10),
        .eye_lower_lid = 0,           .eye_upper_lid = S(16),
        .left_eye_shut = false,       .right_eye_shut = false,
        .brow_offset_y = S(4),        .brow_inner_tilt = S(6),      .brow_spread = S(-4),
        .brow_asymmetry = 0,          .brows_visible = true,
        .mouth_width = S(MOUTH_W-24), .mouth_curve = S(1),          .mouth_open_height = 0,
        .mouth_offset_y = S(2),
        .blush_opacity = 0,           .look_x = 0,                  .look_y = 0,
        .face_offset_x = 0,           .face_offset_y = 0,
    },

    /* Up to something, and pleased about it. The head tilt is the whole joke. */
    [FACE_EMOTION_PLAYFUL] = {
        .eye_width = S(EYE_W+6),      .eye_height = S(EYE_H-4),     .eye_corner_radius = S(EYE_R+6),
        .eye_lower_lid = S(12),       .eye_upper_lid = 0,
        .left_eye_shut = false,       .right_eye_shut = false,
        .brow_offset_y = S(-10),      .brow_inner_tilt = S(-4),     .brow_spread = S(2),
        .brow_asymmetry = S(-15),     .brows_visible = true,
        .mouth_width = S(MOUTH_W+8),  .mouth_curve = S(17),         .mouth_open_height = S(20),
        .mouth_offset_y = S(1),
        .blush_opacity = 160,         .look_x = -18,                .look_y = -8,
        .face_offset_x = S(-4),       .face_offset_y = S(-3),
    },

    /* Past sleepy and into done. Brows sag rather than lift. */
    [FACE_EMOTION_EXHAUSTED] = {
        .eye_width = S(EYE_W-4),      .eye_height = S(EYE_H-34),    .eye_corner_radius = S(EYE_R),
        .eye_lower_lid = 0,           .eye_upper_lid = S(34),
        .left_eye_shut = false,       .right_eye_shut = false,
        .brow_offset_y = S(8),        .brow_inner_tilt = S(-7),     .brow_spread = S(3),
        .brow_asymmetry = S(4),       .brows_visible = true,
        .mouth_width = S(MOUTH_W-20), .mouth_curve = S(-7),         .mouth_open_height = S(8),
        .mouth_offset_y = S(6),
        .blush_opacity = 0,           .look_x = 0,                  .look_y = 20,
        .face_offset_x = 0,           .face_offset_y = S(4),
    },

    [FACE_EMOTION_LISTENING] = {
        .eye_width = S(EYE_W+10),     .eye_height = S(EYE_H+8),     .eye_corner_radius = S(EYE_R+8),
        .eye_lower_lid = 0,           .eye_upper_lid = 0,
        .left_eye_shut = false,       .right_eye_shut = false,
        .brow_offset_y = S(-13),      .brow_inner_tilt = S(-4),     .brow_spread = S(2),
        .brow_asymmetry = 0,          .brows_visible = true,
        .mouth_width = S(MOUTH_W-54), .mouth_curve = 0,             .mouth_open_height = S(24),
        .mouth_offset_y = 0,
        .blush_opacity = 40,          .look_x = 0,                  .look_y = -8,
        .face_offset_x = 0,           .face_offset_y = S(-2),
    },

    [FACE_EMOTION_THINKING] = {
        .eye_width = S(EYE_W-3),      .eye_height = S(EYE_H-5),     .eye_corner_radius = S(EYE_R),
        .eye_lower_lid = 0,           .eye_upper_lid = S(12),
        .left_eye_shut = false,       .right_eye_shut = false,
        .brow_offset_y = S(-6),       .brow_inner_tilt = S(4),      .brow_spread = 0,
        .brow_asymmetry = S(-9),      .brows_visible = true,
        .mouth_width = S(MOUTH_W-40), .mouth_curve = S(-4),         .mouth_open_height = 0,
        .mouth_offset_y = S(1),
        .blush_opacity = 0,           .look_x = 55,                 .look_y = -48,
        .face_offset_x = 0,           .face_offset_y = 0,
    },

    [FACE_EMOTION_SPEAKING] = {
        .eye_width = S(EYE_W+2),      .eye_height = S(EYE_H+2),     .eye_corner_radius = S(EYE_R+2),
        .eye_lower_lid = 0,           .eye_upper_lid = 0,
        .left_eye_shut = false,       .right_eye_shut = false,
        .brow_offset_y = S(-5),       .brow_inner_tilt = S(-2),     .brow_spread = 0,
        .brow_asymmetry = 0,          .brows_visible = true,
        .mouth_width = S(MOUTH_W),    .mouth_curve = S(14),         .mouth_open_height = S(18),
        .mouth_offset_y = 0,
        .blush_opacity = 60,          .look_x = 0,                  .look_y = 0,
        .face_offset_x = 0,           .face_offset_y = 0,
    },
};

/** Lower-case names, used for logging and for parsing the cloud's mood tag. */
static const char *const s_emotion_names[FACE_EMOTION_COUNT] = {
    [FACE_EMOTION_NEUTRAL]   = "neutral",
    [FACE_EMOTION_HAPPY]     = "happy",
    [FACE_EMOTION_EXCITED]   = "excited",
    [FACE_EMOTION_CURIOUS]   = "curious",
    [FACE_EMOTION_SURPRISED] = "surprised",
    [FACE_EMOTION_SAD]       = "sad",
    [FACE_EMOTION_ANNOYED]   = "annoyed",
    [FACE_EMOTION_SLEEPY]    = "sleepy",
    [FACE_EMOTION_YAWN]      = "yawn",
    [FACE_EMOTION_ASLEEP]    = "asleep",
    [FACE_EMOTION_WINK]      = "wink",
    [FACE_EMOTION_LOVE]      = "love",
    [FACE_EMOTION_CONFUSED]  = "confused",
    [FACE_EMOTION_DIZZY]     = "dizzy",
    [FACE_EMOTION_MISCHIEF]  = "mischief",
    [FACE_EMOTION_PROUD]     = "proud",
    [FACE_EMOTION_BORED]      = "bored",
    [FACE_EMOTION_SMUG]       = "smug",
    [FACE_EMOTION_SHOCKED]    = "shocked",
    [FACE_EMOTION_DETERMINED] = "determined",
    [FACE_EMOTION_GIGGLY]     = "giggly",
    [FACE_EMOTION_SUSPICIOUS] = "suspicious",
    [FACE_EMOTION_RELIEVED]   = "relieved",
    [FACE_EMOTION_AMAZED]     = "amazed",
    [FACE_EMOTION_NERVOUS]     = "nervous",
    [FACE_EMOTION_PLEADING]    = "pleading",
    [FACE_EMOTION_TRIUMPHANT]  = "triumphant",
    [FACE_EMOTION_UNIMPRESSED] = "unimpressed",
    [FACE_EMOTION_SCRUNCH]     = "scrunch",
    [FACE_EMOTION_SHY]         = "shy",
    [FACE_EMOTION_PANIC]       = "panic",
    [FACE_EMOTION_DISGUSTED]   = "disgusted",
    [FACE_EMOTION_SINGING]     = "singing",
    [FACE_EMOTION_STARRY]     = "starry",
    [FACE_EMOTION_SCEPTICAL]  = "sceptical",
    [FACE_EMOTION_SHEEPISH]   = "sheepish",
    [FACE_EMOTION_HOPEFUL]    = "hopeful",
    [FACE_EMOTION_THOUGHTFUL] = "thoughtful",
    [FACE_EMOTION_CHEEKY]     = "cheeky",
    [FACE_EMOTION_ALARMED]    = "alarmed",
    [FACE_EMOTION_COSY]       = "cosy",
    [FACE_EMOTION_DREAMY]     = "dreamy",
    [FACE_EMOTION_FOCUSED]    = "focused",
    [FACE_EMOTION_PLAYFUL]    = "playful",
    [FACE_EMOTION_EXHAUSTED]  = "exhausted",
    [FACE_EMOTION_LISTENING] = "listening",
    [FACE_EMOTION_THINKING]  = "thinking",
    [FACE_EMOTION_SPEAKING]  = "speaking",
};

const face_pose_t *face_emotion_get_pose(face_emotion_t emotion)
{
    if (emotion < 0 || emotion >= FACE_EMOTION_COUNT) {
        emotion = FACE_EMOTION_NEUTRAL;
    }
    return &s_emotion_poses[emotion];
}

const char *face_emotion_get_name(face_emotion_t emotion)
{
    if (emotion < 0 || emotion >= FACE_EMOTION_COUNT || s_emotion_names[emotion] == NULL) {
        return "neutral";
    }
    return s_emotion_names[emotion];
}

bool face_emotion_from_name(const char *name, size_t name_len, face_emotion_t *out)
{
    if (name == NULL || out == NULL || name_len == 0) {
        return false;
    }

    for (int i = 0; i < FACE_EMOTION_COUNT; i++) {
        const char *candidate = s_emotion_names[i];
        if (candidate == NULL) {
            continue;
        }
        if (strlen(candidate) == name_len && strncasecmp(candidate, name, name_len) == 0) {
            *out = (face_emotion_t)i;
            return true;
        }
    }
    return false;
}

/* ------------------------------------------------------------------------ */
/* The idle antics                                                           */
/* ------------------------------------------------------------------------ */

/* A slow sweep of the eyes to the left and then the right. */
static const face_antic_step_t s_antic_glance_around[] = {
    { FACE_EMOTION_CURIOUS,  -70,  10,  0, 0, 260, 620 },
    { FACE_EMOTION_CURIOUS,   70,  10,  0, 0, 420, 620 },
    { FACE_EMOTION_NEUTRAL,    0,   0,  0, 0, 320,   0 },
};

/* A quick, cheeky wink. */
static const face_antic_step_t s_antic_wink[] = {
    { FACE_EMOTION_WINK,       0,   0,  0, 0, 140, 520 },
    { FACE_EMOTION_HAPPY,      0,   0,  0, 0, 180, 380 },
    { FACE_EMOTION_NEUTRAL,    0,   0,  0, 0, 260,   0 },
};

/* Eyes travel all the way around the socket, like a theatrical eye-roll. */
static const face_antic_step_t s_antic_eye_roll[] = {
    { FACE_EMOTION_ANNOYED,  -60, -70,  0, 0, 200, 120 },
    { FACE_EMOTION_ANNOYED,    0, -90,  0, 0, 180, 100 },
    { FACE_EMOTION_ANNOYED,   60, -70,  0, 0, 180, 100 },
    { FACE_EMOTION_ANNOYED,   60,  60,  0, 0, 180, 100 },
    { FACE_EMOTION_NEUTRAL,    0,   0,  0, 0, 300,   0 },
};

/* Two little bounces of the whole face, like a delighted hop. */
static const face_antic_step_t s_antic_happy_bounce[] = {
    { FACE_EMOTION_HAPPY,      0,   0,  0, -9, 150, 90 },
    { FACE_EMOTION_HAPPY,      0,   0,  0,  4, 130, 70 },
    { FACE_EMOTION_HAPPY,      0,   0,  0, -6, 130, 90 },
    { FACE_EMOTION_NEUTRAL,    0,   0,  0,  0, 260,  0 },
};

/* A startled snap to attention that settles back down. */
static const face_antic_step_t s_antic_startle[] = {
    { FACE_EMOTION_SURPRISED,  0,   0,  0, -5,  90, 420 },
    { FACE_EMOTION_CURIOUS,   30,   0,  0,  0, 220, 300 },
    { FACE_EMOTION_NEUTRAL,    0,   0,  0,  0, 280,   0 },
};

/* A big sleepy yawn. */
static const face_antic_step_t s_antic_yawn[] = {
    { FACE_EMOTION_SLEEPY,     0,  20,  0,  0, 340, 260 },
    { FACE_EMOTION_YAWN,       0,  10,  0,  3, 300, 620 },
    { FACE_EMOTION_SLEEPY,     0,  20,  0,  0, 280, 240 },
    { FACE_EMOTION_NEUTRAL,    0,   0,  0,  0, 320,   0 },
};

/* A small "no" shake of the head. */
static const face_antic_step_t s_antic_head_shake[] = {
    { FACE_EMOTION_NEUTRAL,  -20,   0, -9, 0, 130, 40 },
    { FACE_EMOTION_NEUTRAL,   20,   0,  9, 0, 150, 40 },
    { FACE_EMOTION_NEUTRAL,  -14,   0, -6, 0, 140, 40 },
    { FACE_EMOTION_NEUTRAL,    0,   0,  0, 0, 200,  0 },
};

/* A single agreeable nod. */
static const face_antic_step_t s_antic_nod[] = {
    { FACE_EMOTION_HAPPY,      0,  30,  0,  7, 150, 60 },
    { FACE_EMOTION_HAPPY,      0, -10,  0, -4, 150, 60 },
    { FACE_EMOTION_HAPPY,      0,  20,  0,  5, 140, 90 },
    { FACE_EMOTION_NEUTRAL,    0,   0,  0,  0, 240,  0 },
};

/* Cocked brow, sideways glance — as if it just overheard something. */
static const face_antic_step_t s_antic_eavesdrop[] = {
    { FACE_EMOTION_CURIOUS,   80,  -6,  4, 0, 260, 700 },
    { FACE_EMOTION_CONFUSED, -50,  -6, -4, 0, 320, 520 },
    { FACE_EMOTION_NEUTRAL,    0,   0,  0, 0, 300,   0 },
};

/* A brief flash of affection. */
static const face_antic_step_t s_antic_love_flash[] = {
    { FACE_EMOTION_LOVE,       0,  -6,  0, -4, 200, 700 },
    { FACE_EMOTION_HAPPY,      0,   0,  0,  0, 240, 260 },
    { FACE_EMOTION_NEUTRAL,    0,   0,  0,  0, 260,   0 },
};

/* Peers down at the desk, then back up at you. */
static const face_antic_step_t s_antic_peek_down[] = {
    { FACE_EMOTION_NEUTRAL,    0,  80,  0, 4, 280, 480 },
    { FACE_EMOTION_SURPRISED,  0, -20,  0, -3, 160, 300 },
    { FACE_EMOTION_HAPPY,      0,   0,  0,  0, 220, 260 },
    { FACE_EMOTION_NEUTRAL,    0,   0,  0,  0, 240,   0 },
};

/* A dizzy little wobble. */
static const face_antic_step_t s_antic_wobble[] = {
    { FACE_EMOTION_DIZZY,    -40,   0, -7, 2, 200, 220 },
    { FACE_EMOTION_DIZZY,     40,   0,  7, -2, 220, 220 },
    { FACE_EMOTION_DIZZY,      0,   0,  0, 0, 200, 180 },
    { FACE_EMOTION_HAPPY,      0,   0,  0, 0, 260, 200 },
    { FACE_EMOTION_NEUTRAL,    0,   0,  0, 0, 240,   0 },
};

/* Convenience macro so the registry below stays one readable line per antic. */
#define ANTIC_ENTRY(display_name, step_array) \
    { .name = (display_name), .steps = (step_array), \
      .step_count = (uint8_t)(sizeof(step_array) / sizeof((step_array)[0])) }

/**
 * The registry the animator picks from at random. Add new antics here and they
 * are immediately part of the buddy's repertoire — no other file changes.
 */
static const face_antic_t s_antics[] = {
    ANTIC_ENTRY("glance around", s_antic_glance_around),
    ANTIC_ENTRY("wink",          s_antic_wink),
    ANTIC_ENTRY("eye roll",      s_antic_eye_roll),
    ANTIC_ENTRY("happy bounce",  s_antic_happy_bounce),
    ANTIC_ENTRY("startle",       s_antic_startle),
    ANTIC_ENTRY("yawn",          s_antic_yawn),
    ANTIC_ENTRY("head shake",    s_antic_head_shake),
    ANTIC_ENTRY("nod",           s_antic_nod),
    ANTIC_ENTRY("eavesdrop",     s_antic_eavesdrop),
    ANTIC_ENTRY("love flash",    s_antic_love_flash),
    ANTIC_ENTRY("peek down",     s_antic_peek_down),
    ANTIC_ENTRY("wobble",        s_antic_wobble),
};

uint8_t face_antic_get_count(void)
{
    return (uint8_t)(sizeof(s_antics) / sizeof(s_antics[0]));
}

const face_antic_t *face_antic_get(uint8_t index)
{
    if (index >= face_antic_get_count()) {
        return NULL;
    }
    return &s_antics[index];
}
