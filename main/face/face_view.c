/*
 * face_view.c — Turning a pose into LVGL widgets.
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */

#include <stdlib.h>
#include <string.h>

#include "esp_log.h"

#include "buddy_config.h"
#include "face_emotions.h"
#include "face_theme.h"
#include "face_view.h"

static const char *TAG = "face_view";

/* ------------------------------------------------------------------------ */
/* Internal drawing constants                                                */
/* ------------------------------------------------------------------------ */

/** Horizontal centre of the screen — the face is symmetrical about this line. */
#define SCREEN_CENTRE_X             (BUDDY_SCREEN_WIDTH / 2)

/** Height of a fully shut eye (a wink or a sleeping eye), in pixels. */
#define EYE_SHUT_HEIGHT             16

/** How much of a shut eye is carved away from below, to curve it into a smile. */
#define EYE_SHUT_CRESCENT           9

/*
 * The eyebrows are drawn inside a fixed-size invisible canvas so that tilting
 * them never changes the object's size — only the points inside it move. That
 * keeps positioning simple and stops the brows from drifting as they animate.
 */
#define BROW_CANVAS_WIDTH           (FACE_BROW_LENGTH + FACE_BROW_THICKNESS)
#define BROW_CANVAS_HEIGHT          44
#define BROW_BASELINE_Y             22

/** A gentle upward bow in the middle of each brow — it reads as friendly. */
#define BROW_ARCH                   3

/** The mouth canvas, sized generously so a wide grin never gets clipped. */
#define MOUTH_CANVAS_WIDTH          200
#define MOUTH_CANVAS_HEIGHT         80
#define MOUTH_BASELINE_Y            40

/*
 * Where the closed mouth stops being a drawn line and becomes an opening.
 *
 * Set to the stroke thickness on purpose: at exactly that height a filled pill
 * and the line are the same shape, so the swap between them cannot be seen.
 * See layout_mouth() for why this is a hard switch rather than a cross-fade.
 */
#define MOUTH_BECOMES_OPENING_AT    FACE_MOUTH_THICKNESS

/** Distinct addresses used to tell our two animations apart. */
static uint8_t s_pose_anim_tag;
static uint8_t s_shift_anim_tag;

/* ------------------------------------------------------------------------ */
/* Widget handles and state                                                  */
/* ------------------------------------------------------------------------ */

/** Index into the paired arrays below. */
enum { SIDE_LEFT = 0, SIDE_RIGHT = 1, SIDE_COUNT };

static lv_obj_t *s_root;                       /*!< Transparent full-screen host */
static lv_obj_t *s_eye[SIDE_COUNT];
static lv_obj_t *s_eye_lower_cutter[SIDE_COUNT];
static lv_obj_t *s_eye_upper_cutter[SIDE_COUNT];
static lv_obj_t *s_brow[SIDE_COUNT];
static lv_obj_t *s_blush[SIDE_COUNT];
static lv_obj_t *s_mouth_line;
static lv_obj_t *s_mouth_open;

/*
 * LVGL stores only a POINTER to line points, it does not copy them, so these
 * arrays have to outlive every draw. Static storage does that for free.
 */
static lv_point_t s_brow_points[SIDE_COUNT][3];
static lv_point_t s_mouth_points[FACE_MOUTH_POINT_COUNT];

/** Where the face is right now, where it started, and where it is heading. */
static face_pose_t s_pose_current;
static face_pose_t s_pose_from;
static face_pose_t s_pose_to;

/** Extra vertical offset applied on top of the pose (speech-bubble lift). */
static int16_t s_vertical_shift;

/*
 * The last values actually pushed into LVGL.
 *
 * Position and size setters skip the work when nothing changed, but the style
 * setters do not: they invalidate the widget every single time they are called.
 * During lip sync the mouth is updated eighteen times a second, and without
 * these guards that would repaint the eyes, brows and blush too — enough extra
 * drawing to make the whole face stutter. Comparing first costs a few bytes and
 * keeps the redraws down to the parts that genuinely moved.
 */
static int16_t   s_applied_eye_radius[SIDE_COUNT];
static bool      s_applied_brows_visible = true;
static uint8_t   s_applied_blush_opacity;
static bool      s_applied_line_hidden = false;
static bool      s_applied_pill_hidden = true;
static lv_point_t s_applied_brow_points[SIDE_COUNT][3];
static lv_point_t s_applied_mouth_points[FACE_MOUTH_POINT_COUNT];

static bool s_created;

/* ------------------------------------------------------------------------ */
/* Small helpers                                                             */
/* ------------------------------------------------------------------------ */

static int16_t clamp_i16(int16_t value, int16_t low, int16_t high)
{
    if (value < low) {
        return low;
    }
    if (value > high) {
        return high;
    }
    return value;
}

/**
 * @brief Create a bare rectangle with no theme styling, padding or scrolling.
 *
 * Every shape in the face starts life this way, so none of them inherit the
 * default widget look (borders, shadows, padding) that would ruin the drawing.
 */
static lv_obj_t *create_bare_shape(lv_obj_t *parent, uint32_t hex_colour)
{
    lv_obj_t *shape = lv_obj_create(parent);
    lv_obj_remove_style_all(shape);
    lv_obj_clear_flag(shape, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(shape, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(shape, lv_color_hex(hex_colour), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(shape, LV_OPA_COVER, LV_PART_MAIN);
    return shape;
}

/**
 * @brief Create one eyebrow / mouth stroke as a rounded polyline.
 */
static lv_obj_t *create_stroke(lv_obj_t *parent, lv_coord_t width, lv_coord_t height,
                               lv_coord_t thickness)
{
    lv_obj_t *line = lv_line_create(parent);
    lv_obj_remove_style_all(line);
    lv_obj_clear_flag(line, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(line, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(line, width, height);
    lv_obj_set_style_line_color(line, FACE_COLOR_ESPRESSIF_RED, LV_PART_MAIN);
    lv_obj_set_style_line_width(line, thickness, LV_PART_MAIN);
    lv_obj_set_style_line_rounded(line, true, LV_PART_MAIN);
    return line;
}

/* ------------------------------------------------------------------------ */
/* Laying the pose out on screen                                             */
/* ------------------------------------------------------------------------ */

/**
 * @brief Position and size both eyes, including their crescent cutters.
 */
static void layout_eyes(const face_pose_t *pose, int16_t face_dx, int16_t face_dy,
                        int16_t look_dx, int16_t look_dy)
{
    for (int side = 0; side < SIDE_COUNT; side++) {
        const bool shut = (side == SIDE_LEFT) ? pose->left_eye_shut : pose->right_eye_shut;

        /* A shut eye ignores the pose's height and becomes a thin curved bar. */
        int16_t width  = clamp_i16(pose->eye_width, 8, 160);
        int16_t height = shut ? EYE_SHUT_HEIGHT : clamp_i16(pose->eye_height, 4, 200);
        int16_t radius = shut ? (EYE_SHUT_HEIGHT / 2) : pose->eye_corner_radius;
        int16_t lower  = shut ? EYE_SHUT_CRESCENT : pose->eye_lower_lid;
        int16_t upper  = shut ? 0 : pose->eye_upper_lid;

        /* A corner radius larger than half the shorter side would look broken. */
        int16_t max_radius = ((width < height) ? width : height) / 2;
        radius = clamp_i16(radius, 0, max_radius);
        lower  = clamp_i16(lower, 0, height);
        upper  = clamp_i16(upper, 0, height);

        const int16_t spacing = (side == SIDE_LEFT) ? -FACE_EYE_SPACING_FROM_CENTRE
                                                    : FACE_EYE_SPACING_FROM_CENTRE;
        const int16_t centre_x = SCREEN_CENTRE_X + spacing + face_dx + look_dx;
        const int16_t centre_y = FACE_EYE_CENTRE_Y + face_dy + look_dy;

        lv_obj_set_size(s_eye[side], width, height);
        lv_obj_set_pos(s_eye[side], centre_x - width / 2, centre_y - height / 2);

        if (radius != s_applied_eye_radius[side]) {
            s_applied_eye_radius[side] = radius;
            lv_obj_set_style_radius(s_eye[side], radius, LV_PART_MAIN);
        }

        /*
         * The cutters are background-coloured circles wider than the eye. Sliding
         * one up from the bottom leaves a curved sliver of red — a happy squint.
         * Sliding one down from the top leaves the opposite — a heavy eyelid.
         * When a lid value is zero the circle sits entirely outside the eye and
         * is clipped away, so no show/hide bookkeeping is needed.
         */
        const int16_t cutter_size = (width * 2 > 48) ? (width * 2) : 48;
        const int16_t cutter_x = (width - cutter_size) / 2;

        lv_obj_set_size(s_eye_lower_cutter[side], cutter_size, cutter_size);
        lv_obj_set_pos(s_eye_lower_cutter[side], cutter_x, height - lower);

        lv_obj_set_size(s_eye_upper_cutter[side], cutter_size, cutter_size);
        lv_obj_set_pos(s_eye_upper_cutter[side], cutter_x, upper - cutter_size);
    }
}

/**
 * @brief Point and place both eyebrows.
 */
static void layout_brows(const face_pose_t *pose, int16_t face_dx, int16_t face_dy,
                         int16_t look_dx, int16_t look_dy)
{
    if (pose->brows_visible != s_applied_brows_visible) {
        s_applied_brows_visible = pose->brows_visible;
        for (int side = 0; side < SIDE_COUNT; side++) {
            if (pose->brows_visible) {
                lv_obj_clear_flag(s_brow[side], LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_obj_add_flag(s_brow[side], LV_OBJ_FLAG_HIDDEN);
            }
        }
    }
    if (!pose->brows_visible) {
        return;
    }

    const int16_t tilt      = clamp_i16(pose->brow_inner_tilt, -16, 16);
    const int16_t half_pen  = FACE_BROW_THICKNESS / 2;
    const int16_t far_x     = half_pen + FACE_BROW_LENGTH;
    const int16_t middle_x  = half_pen + FACE_BROW_LENGTH / 2;
    const int16_t middle_y  = BROW_BASELINE_Y + tilt / 2 - BROW_ARCH;

    /*
     * On the left brow the inner end (the one nearest the nose) is the RIGHT
     * end of the stroke; on the right brow it is the LEFT end. Tilting the
     * inner ends down reads as a scowl, up as worry — so the sign flips between
     * the two sides.
     */
    lv_point_t candidate[SIDE_COUNT][3];

    candidate[SIDE_LEFT][0] = (lv_point_t){ half_pen, BROW_BASELINE_Y };
    candidate[SIDE_LEFT][1] = (lv_point_t){ middle_x, middle_y };
    candidate[SIDE_LEFT][2] = (lv_point_t){ far_x, BROW_BASELINE_Y + tilt };

    candidate[SIDE_RIGHT][0] = (lv_point_t){ half_pen, BROW_BASELINE_Y + tilt };
    candidate[SIDE_RIGHT][1] = (lv_point_t){ middle_x, middle_y };
    candidate[SIDE_RIGHT][2] = (lv_point_t){ far_x, BROW_BASELINE_Y };

    for (int side = 0; side < SIDE_COUNT; side++) {
        if (memcmp(candidate[side], s_applied_brow_points[side],
                   sizeof(candidate[side])) != 0) {
            memcpy(s_applied_brow_points[side], candidate[side], sizeof(candidate[side]));
            memcpy(s_brow_points[side], candidate[side], sizeof(candidate[side]));
            lv_line_set_points(s_brow[side], s_brow_points[side], 3);
        }
    }

    /*
     * The brows hang a fixed distance above the RESTING eye position rather than
     * above the current one, so blinking does not drag them down with the lids.
     */
    int16_t baseline_y = FACE_EYE_CENTRE_Y
                         - (FACE_EYE_HEIGHT / 2)
                         - FACE_BROW_GAP_ABOVE_EYE
                         + pose->brow_offset_y
                         + face_dy
                         + look_dy / 2;

    /*
     * Keep the brows out of the sensor pill in the top-right corner. Surprise,
     * looking upward and a bouncing antic each lift the brows a little, and all
     * three at once is enough to reach it. See FACE_BROW_MIN_BASELINE_Y.
     */
    if (baseline_y < FACE_BROW_MIN_BASELINE_Y) {
        baseline_y = FACE_BROW_MIN_BASELINE_Y;
    }

    for (int side = 0; side < SIDE_COUNT; side++) {
        const int16_t spacing = (side == SIDE_LEFT)
                                ? -(FACE_EYE_SPACING_FROM_CENTRE + pose->brow_spread)
                                :  (FACE_EYE_SPACING_FROM_CENTRE + pose->brow_spread);
        const int16_t centre_x = SCREEN_CENTRE_X + spacing + face_dx + look_dx / 2;

        /* Only the left brow carries the asymmetry, giving a quizzical look. */
        const int16_t extra_y = (side == SIDE_LEFT) ? pose->brow_asymmetry : 0;

        lv_obj_set_pos(s_brow[side],
                       centre_x - BROW_CANVAS_WIDTH / 2,
                       baseline_y - BROW_BASELINE_Y + extra_y);
    }
}

/**
 * @brief Bend the mouth line into a parabola and size the open-mouth pill.
 */
static void layout_mouth(const face_pose_t *pose, int16_t face_dx, int16_t face_dy,
                         int16_t look_dx, int16_t look_dy)
{
    const int16_t width = clamp_i16(pose->mouth_width, 16, MOUTH_CANVAS_WIDTH - 20);
    const int16_t curve = clamp_i16(pose->mouth_curve, -34, 34);
    const int16_t open  = clamp_i16(pose->mouth_open_height, 0, FACE_MOUTH_MAX_OPEN_HEIGHT);

    /*
     * The closed mouth is a parabola sampled at FACE_MOUTH_POINT_COUNT points.
     * With u running from -1 at the left corner to +1 at the right corner:
     *
     *     y = baseline + curve * (1 - u^2)
     *
     * A positive curve pushes the middle DOWN the screen while the corners stay
     * put, which is exactly a smile. A negative curve lifts the middle into a
     * frown. All integer maths — no floating point in a draw path.
     */
    lv_point_t candidate[FACE_MOUTH_POINT_COUNT];

    const int last = FACE_MOUTH_POINT_COUNT - 1;
    for (int i = 0; i <= last; i++) {
        const int32_t u_numerator = (2 * i) - last;         /* -last .. +last   */
        const int32_t u_squared_scaled = (u_numerator * u_numerator);
        const int32_t denominator = (int32_t)last * last;

        const int16_t x = (int16_t)(MOUTH_CANVAS_WIDTH / 2 - width / 2 + (width * i) / last);
        const int16_t y = (int16_t)(MOUTH_BASELINE_Y + curve
                                    - (curve * u_squared_scaled) / denominator);

        candidate[i] = (lv_point_t){ x, y };
    }

    if (memcmp(candidate, s_applied_mouth_points, sizeof(candidate)) != 0) {
        memcpy(s_applied_mouth_points, candidate, sizeof(candidate));
        memcpy(s_mouth_points, candidate, sizeof(candidate));
        lv_line_set_points(s_mouth_line, s_mouth_points, FACE_MOUTH_POINT_COUNT);
    }

    const int16_t centre_x = SCREEN_CENTRE_X + face_dx + look_dx / 3;
    const int16_t centre_y = FACE_MOUTH_CENTRE_Y + pose->mouth_offset_y
                             + face_dy + look_dy / 3;

    lv_obj_set_pos(s_mouth_line,
                   centre_x - MOUTH_CANVAS_WIDTH / 2,
                   centre_y - MOUTH_BASELINE_Y);

    /*
     * ONE mouth shape on screen at a time.
     *
     * This used to cross-fade: through the whole middle of the range BOTH the
     * curved line and the filled pill were drawn at partial opacity. Where the
     * two overlapped the red doubled up, and where they did not it stayed pale,
     * so the mouth wore a permanent ghost of itself. Worse, they were anchored
     * differently — the pill sat at curve/3 while the line's bowl was at curve —
     * so the ghost was offset rather than concentric. Lip sync sweeps through
     * that band about eighteen times a second, which is what made the face look
     * wrong in a way that was hard to point at.
     *
     * The hand-over is now a single clean switch, at the one height where the
     * two shapes are indistinguishable: an opening as tall as the line is thick
     * looks the same drawn either way. Nothing overlaps, nothing pops, and
     * neither shape is ever alpha-blended — which is also the cheapest thing to
     * draw, so the face got faster at the same time.
     */
    const bool use_pill    = (open > MOUTH_BECOMES_OPENING_AT);
    const bool line_hidden = use_pill;
    const bool pill_hidden = !use_pill;

    /*
     * Each flag is compared against what is ALREADY on screen, not against
     * use_pill. Toggling a flag LVGL has already set is not free — it
     * invalidates the object's area — and doing it on every frame of lip sync
     * would hand back the redraw this change was meant to save.
     */
    if (line_hidden != s_applied_line_hidden) {
        s_applied_line_hidden = line_hidden;
        if (line_hidden) {
            lv_obj_add_flag(s_mouth_line, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_clear_flag(s_mouth_line, LV_OBJ_FLAG_HIDDEN);
        }
    }

    if (pill_hidden != s_applied_pill_hidden) {
        s_applied_pill_hidden = pill_hidden;
        if (pill_hidden) {
            lv_obj_add_flag(s_mouth_open, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_clear_flag(s_mouth_open, LV_OBJ_FLAG_HIDDEN);
        }
    }

    if (use_pill) {
        /*
         * Centred on the LOW POINT of the smile rather than on the baseline,
         * so the opening sits inside the bowl of the mouth instead of floating
         * above it. That single term is what stops an open smile reading as a
         * separate shape stuck on the face.
         */
        lv_obj_set_size(s_mouth_open, width, open);
        lv_obj_set_pos(s_mouth_open,
                       centre_x - width / 2,
                       centre_y + curve - open / 2);
    }
}

/**
 * @brief Place and fade the two blush circles.
 */
static void layout_blush(const face_pose_t *pose, int16_t face_dx, int16_t face_dy,
                         int16_t look_dx, int16_t look_dy)
{
    /*
     * Most expressions have no blush at all. Hiding the circles outright,
     * rather than leaving them on screen at zero opacity, is worth doing: an
     * invisible object still gets repositioned on every frame, and every
     * reposition invalidates both the area it left and the area it arrived in.
     * That was two pointless redraw regions on almost every frame of every
     * animation, for something nobody could see.
     */
    const bool visible = (pose->blush_opacity > 0);

    if (visible != (s_applied_blush_opacity > 0)) {
        for (int side = 0; side < SIDE_COUNT; side++) {
            if (visible) {
                lv_obj_clear_flag(s_blush[side], LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_obj_add_flag(s_blush[side], LV_OBJ_FLAG_HIDDEN);
            }
        }
    }

    if (!visible) {
        s_applied_blush_opacity = 0;
        return;
    }

    for (int side = 0; side < SIDE_COUNT; side++) {
        const int16_t spacing = (side == SIDE_LEFT) ? -FACE_BLUSH_OFFSET_X
                                                    : FACE_BLUSH_OFFSET_X;
        lv_obj_set_pos(s_blush[side],
                       SCREEN_CENTRE_X + spacing + face_dx + look_dx / 2
                           - FACE_BLUSH_DIAMETER / 2,
                       FACE_BLUSH_OFFSET_Y + face_dy + look_dy / 2
                           - FACE_BLUSH_DIAMETER / 2);
    }

    if (pose->blush_opacity != s_applied_blush_opacity) {
        s_applied_blush_opacity = pose->blush_opacity;
        for (int side = 0; side < SIDE_COUNT; side++) {
            lv_obj_set_style_bg_opa(s_blush[side], pose->blush_opacity, LV_PART_MAIN);
        }
    }
}

void face_view_apply_pose(const face_pose_t *pose)
{
    if (!s_created || pose == NULL) {
        return;
    }

    /*
     * The cheapest frame is the one that never happens.
     *
     * Animations tick at the display refresh rate whether or not the numbers
     * they produce have moved, and blending two poses that are already close
     * together yields the same rounded integers several frames running.
     * Comparing the finished pose against the last one applied makes those
     * frames cost one memcmp instead of a full relayout and every invalidation
     * that follows it. On a face that is mostly sitting still, that is most of
     * the frames.
     */
    static face_pose_t s_last_applied;
    static int16_t     s_last_shift;
    static bool        s_have_applied;

    if (s_have_applied &&
        s_last_shift == s_vertical_shift &&
        memcmp(&s_last_applied, pose, sizeof(*pose)) == 0) {
        return;
    }

    s_last_applied = *pose;
    s_last_shift   = s_vertical_shift;
    s_have_applied = true;

    s_pose_current = *pose;

    /* Whole-face nudge plus the speech-bubble lift. */
    const int16_t face_dx = pose->face_offset_x;
    const int16_t face_dy = pose->face_offset_y + s_vertical_shift;

    /* Gaze direction, converted from -100..100 into pixels. */
    const int16_t look_dx =
        (clamp_i16(pose->look_x, -100, 100) * FACE_EYE_LOOK_RANGE_X) / 100;
    const int16_t look_dy =
        (clamp_i16(pose->look_y, -100, 100) * FACE_EYE_LOOK_RANGE_Y) / 100;

    layout_eyes(pose, face_dx, face_dy, look_dx, look_dy);
    layout_brows(pose, face_dx, face_dy, look_dx, look_dy);
    layout_mouth(pose, face_dx, face_dy, look_dx, look_dy);
    layout_blush(pose, face_dx, face_dy, look_dx, look_dy);
}

/* ------------------------------------------------------------------------ */
/* Animation                                                                 */
/* ------------------------------------------------------------------------ */

/**
 * @brief Animation step: blend from -> to and repaint.
 */
static void pose_anim_step_cb(void *unused, int32_t progress)
{
    (void)unused;
    face_pose_t blended;
    face_pose_blend(&s_pose_from, &s_pose_to, (uint8_t)progress, &blended);
    face_view_apply_pose(&blended);
}

/**
 * @brief Animation step for the whole-face vertical slide.
 */
static void shift_anim_step_cb(void *unused, int32_t value)
{
    (void)unused;
    s_vertical_shift = (int16_t)value;
    face_view_apply_pose(&s_pose_current);
}

void face_view_animate_to(const face_pose_t *target, uint16_t duration_ms)
{
    if (!s_created || target == NULL) {
        return;
    }

    /* Cancel any glide in flight so the new one starts from where we are now. */
    lv_anim_del(&s_pose_anim_tag, pose_anim_step_cb);

    s_pose_from = s_pose_current;
    s_pose_to   = *target;

    if (duration_ms == 0) {
        face_view_apply_pose(target);
        return;
    }

    lv_anim_t anim;
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, &s_pose_anim_tag);
    lv_anim_set_exec_cb(&anim, pose_anim_step_cb);
    lv_anim_set_values(&anim, 0, 255);
    lv_anim_set_time(&anim, duration_ms);
    lv_anim_set_path_cb(&anim, lv_anim_path_ease_in_out);
    lv_anim_start(&anim);
}

void face_view_get_current_pose(face_pose_t *out)
{
    if (out != NULL) {
        *out = s_pose_current;
    }
}

void face_view_get_target_pose(face_pose_t *out)
{
    if (out != NULL) {
        *out = s_pose_to;
    }
}

void face_view_set_mouth_opening(int16_t open_height)
{
    if (!s_created) {
        return;
    }
    /*
     * Update both the live pose and the animation target. Writing to the target
     * too means a glide that finishes right after this call will not snap the
     * mouth back shut mid-sentence.
     */
    s_pose_current.mouth_open_height = open_height;
    s_pose_to.mouth_open_height = open_height;
    face_view_apply_pose(&s_pose_current);
}

void face_view_set_vertical_shift(int16_t pixels, uint16_t duration_ms)
{
    if (!s_created) {
        return;
    }

    lv_anim_del(&s_shift_anim_tag, shift_anim_step_cb);

    if (duration_ms == 0) {
        s_vertical_shift = pixels;
        face_view_apply_pose(&s_pose_current);
        return;
    }

    lv_anim_t anim;
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, &s_shift_anim_tag);
    lv_anim_set_exec_cb(&anim, shift_anim_step_cb);
    lv_anim_set_values(&anim, s_vertical_shift, pixels);
    lv_anim_set_time(&anim, duration_ms);
    lv_anim_set_path_cb(&anim, lv_anim_path_ease_out);
    lv_anim_start(&anim);
}

void face_view_set_hidden(bool hidden)
{
    if (!s_created) {
        return;
    }
    if (hidden) {
        lv_obj_add_flag(s_root, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_clear_flag(s_root, LV_OBJ_FLAG_HIDDEN);
    }
}

/* ------------------------------------------------------------------------ */
/* Construction                                                              */
/* ------------------------------------------------------------------------ */

esp_err_t face_view_create(lv_obj_t *parent)
{
    if (parent == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_created) {
        return ESP_OK;
    }

    /* A transparent container so the whole face can be hidden in one call. */
    s_root = lv_obj_create(parent);
    lv_obj_remove_style_all(s_root);
    lv_obj_set_size(s_root, BUDDY_SCREEN_WIDTH, BUDDY_SCREEN_HEIGHT);
    lv_obj_set_pos(s_root, 0, 0);
    lv_obj_clear_flag(s_root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(s_root, LV_OBJ_FLAG_CLICKABLE);

    /* Blush goes in first so it sits behind everything else. */
    for (int side = 0; side < SIDE_COUNT; side++) {
        s_blush[side] = create_bare_shape(s_root, FACE_HEX_BLUSH);
        lv_obj_set_size(s_blush[side], FACE_BLUSH_DIAMETER, FACE_BLUSH_DIAMETER);
        lv_obj_set_style_radius(s_blush[side], LV_RADIUS_CIRCLE, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(s_blush[side], LV_OPA_TRANSP, LV_PART_MAIN);
    }

    /* Eyes, each carrying the two crescent cutters that shape the lids. */
    for (int side = 0; side < SIDE_COUNT; side++) {
        s_eye[side] = create_bare_shape(s_root, FACE_HEX_ESPRESSIF_RED);
        /* Clip the cutters to the eye's rounded outline, not just its box. */
        lv_obj_set_style_clip_corner(s_eye[side], true, LV_PART_MAIN);

        s_eye_lower_cutter[side] = create_bare_shape(s_eye[side], FACE_HEX_BACKGROUND);
        lv_obj_set_style_radius(s_eye_lower_cutter[side], LV_RADIUS_CIRCLE, LV_PART_MAIN);

        s_eye_upper_cutter[side] = create_bare_shape(s_eye[side], FACE_HEX_BACKGROUND);
        lv_obj_set_style_radius(s_eye_upper_cutter[side], LV_RADIUS_CIRCLE, LV_PART_MAIN);
    }

    /* Eyebrows. */
    for (int side = 0; side < SIDE_COUNT; side++) {
        s_brow[side] = create_stroke(s_root, BROW_CANVAS_WIDTH, BROW_CANVAS_HEIGHT,
                                     FACE_BROW_THICKNESS);
    }

    /* Mouth: the curved line and the filled opening that replaces it. */
    s_mouth_line = create_stroke(s_root, MOUTH_CANVAS_WIDTH, MOUTH_CANVAS_HEIGHT,
                                 FACE_MOUTH_THICKNESS);

    s_mouth_open = create_bare_shape(s_root, FACE_HEX_ESPRESSIF_RED);
    lv_obj_set_style_radius(s_mouth_open, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_add_flag(s_mouth_open, LV_OBJ_FLAG_HIDDEN);

    s_created = true;
    s_vertical_shift = 0;

    /* Start life with a calm, gently smiling face. */
    const face_pose_t *neutral = face_emotion_get_pose(FACE_EMOTION_NEUTRAL);
    s_pose_from = *neutral;
    s_pose_to = *neutral;
    face_view_apply_pose(neutral);

    ESP_LOGI(TAG, "Face widgets created");
    return ESP_OK;
}
