/*
 * ui_speech_bubble.c — Drawing the caption bar.
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */

#include <string.h>

#include "esp_log.h"

#include "buddy_config.h"
#include "face_theme.h"
#include "face_view.h"
#include "ui_speech_bubble.h"

static const char *TAG = "ui_bubble";

/** How long the face takes to slide up or down when the bubble appears. */
#define FACE_SLIDE_MS       220

/*
 * The caption's own entrance: it rises a few pixels and fades in, finishing
 * just before the face settles, so the two read as one movement. Leaving is
 * quicker than arriving. Both animate colours' alpha and a translation only,
 * never the object-wide "opa" style: in LVGL 8 that renders the bubble through
 * an off-screen layer buffer, which is internal RAM this build does not have.
 */
#define CAPTION_ENTER_MS    180
#define CAPTION_EXIT_MS     120
#define CAPTION_RISE_PX     8

#if defined(CONFIG_BUDDY_UI_REDUCED_MOTION) && CONFIG_BUDDY_UI_REDUCED_MOTION
#define CAPTION_ANIMATE     0
#else
#define CAPTION_ANIMATE     1
#endif

static lv_obj_t   *s_bubble;
static lv_obj_t   *s_label;
static lv_timer_t *s_hide_timer;
static bool        s_visible;

/*
 * Caption fonts, largest first.
 *
 * A long answer set at 16 px can need more lines than the bubble is allowed to
 * be tall. Rather than clip it — which is what used to happen, and it swallowed
 * half the sentence — the text is measured at each size in turn and the largest
 * one that fits is the one kept.
 *
 * The 12 px tier is what makes "never shortened" actually true: at 14 px a full
 * screen holds around four hundred characters, and BUDDY_REPLY_TEXT_MAX_LEN is
 * 512. It is compiled in only when CONFIG_LV_FONT_MONTSERRAT_12 is set, so the
 * guard keeps the build working either way — and the boot log says which you
 * got, rather than leaving you to discover it from a truncated sentence.
 */
static const lv_font_t *const s_caption_fonts[] = {
    &lv_font_montserrat_16,
    &lv_font_montserrat_14,
#if defined(LV_FONT_MONTSERRAT_12) && LV_FONT_MONTSERRAT_12
    &lv_font_montserrat_12,
#endif
};

#define CAPTION_FONT_COUNT  (sizeof(s_caption_fonts) / sizeof(s_caption_fonts[0]))

/** Widest the text itself may be: the bubble's limit, less padding and border. */
#define CAPTION_MAX_WIDTH \
    (BUDDY_SPEECH_BUBBLE_MAX_WIDTH - (2 * BUDDY_SPEECH_BUBBLE_PADDING) - 2)

/**
 * @brief Lay the text out at the largest font size that fits the bubble.
 *
 * The bubble is sized by its contents, so this works by setting a font, asking
 * LVGL to settle the layout, and reading back how tall the result came out.
 *
 * Nothing here shortens the text. The bubble is allowed to grow over the face
 * if that is what it takes to show a whole answer — the face comes back the
 * moment the caption expires, and a sentence you cannot finish reading is worth
 * less than a face you cannot see for four seconds.
 *
 * @param text  The caption to show.
 * @return Height of the finished bubble, in pixels.
 */
static lv_coord_t fit_bubble_to_text(const char *text)
{
    for (size_t i = 0; i < CAPTION_FONT_COUNT; i++) {
        /*
         * Undo anything the fallback below may have left behind on a previous
         * caption, so this measurement starts from a bubble that is free to
         * grow rather than one already pinned to the maximum.
         */
        lv_label_set_long_mode(s_label, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(s_label, LV_SIZE_CONTENT);
        lv_obj_set_height(s_label, LV_SIZE_CONTENT);
        lv_obj_set_style_max_width(s_label, CAPTION_MAX_WIDTH, LV_PART_MAIN);
        lv_obj_set_style_text_font(s_label, s_caption_fonts[i], LV_PART_MAIN);
        lv_label_set_text(s_label, text);

        lv_obj_update_layout(s_bubble);

        const lv_coord_t height = lv_obj_get_height(s_bubble);
        if (height <= BUDDY_SPEECH_BUBBLE_MAX_HEIGHT) {
            return height;
        }
    }

    /*
     * Longer than a full screen of the smallest font.
     *
     * The bubble stays as it is — wrapped, content-sized, showing everything —
     * rather than being pinned and ellipsised. It is anchored to the bottom of
     * the screen, so an overflowing caption runs off the TOP: the last lines
     * stay readable and the first ones go, which is the right way round for
     * something being read aloud at the same time.
     *
     * Reaching here needs a reply longer than about twelve lines at this width,
     * which is more characters than BUDDY_REPLY_TEXT_MAX_LEN can hold. The
     * branch is kept rather than deleted because it is the place to revisit if
     * that limit ever grows.
     */
    ESP_LOGW(TAG, "Caption is taller than the screen (%d px); showing it anyway",
             (int)lv_obj_get_height(s_bubble));

    return lv_obj_get_height(s_bubble);
}

/**
 * @brief Apply one step of the enter/exit fade: 0 is invisible, 255 is fully in.
 */
static void caption_fade_cb(void *obj, int32_t value)
{
    (void)obj;
    const lv_opa_t opa = (lv_opa_t)value;
    lv_obj_set_style_bg_opa(s_bubble, opa, LV_PART_MAIN);
    lv_obj_set_style_border_opa(s_bubble, opa, LV_PART_MAIN);
    lv_obj_set_style_text_opa(s_label, opa, LV_PART_MAIN);
    lv_obj_set_style_translate_y(s_bubble,
                                 (lv_coord_t)(((255 - value) * CAPTION_RISE_PX) / 255),
                                 LV_PART_MAIN);
}

/** @brief The exit fade finished: now actually hide the bubble. */
static void caption_exit_done_cb(lv_anim_t *anim)
{
    (void)anim;
    if (!s_visible) {
        lv_obj_add_flag(s_bubble, LV_OBJ_FLAG_HIDDEN);
    }
}

/**
 * @brief Run the fade from @p from to @p to. Replaces any fade already running.
 */
static void caption_fade(int32_t from, int32_t to, uint32_t duration_ms,
                         lv_anim_ready_cb_t ready_cb)
{
    lv_anim_del(s_bubble, caption_fade_cb);

#if CAPTION_ANIMATE
    lv_anim_t anim;
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, s_bubble);
    lv_anim_set_exec_cb(&anim, caption_fade_cb);
    lv_anim_set_values(&anim, from, to);
    lv_anim_set_time(&anim, duration_ms);
    lv_anim_set_path_cb(&anim, lv_anim_path_ease_out);
    if (ready_cb != NULL) {
        lv_anim_set_ready_cb(&anim, ready_cb);
    }
    lv_anim_start(&anim);
#else
    (void)from;
    (void)duration_ms;
    caption_fade_cb(s_bubble, to);
    if (ready_cb != NULL) {
        ready_cb(NULL);
    }
#endif
}

/**
 * @brief Timer callback that hides the bubble once its welcome is worn out.
 */
static void auto_hide_timer_cb(lv_timer_t *timer)
{
    (void)timer;   /* Repeat count of 1, so LVGL disposes of it. */
    s_hide_timer = NULL;
    ui_speech_bubble_hide();
}

esp_err_t ui_speech_bubble_create(lv_obj_t *parent)
{
    if (parent == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_bubble != NULL) {
        return ESP_OK;
    }

    s_bubble = lv_obj_create(parent);
    lv_obj_remove_style_all(s_bubble);
    lv_obj_clear_flag(s_bubble, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(s_bubble, LV_OBJ_FLAG_CLICKABLE);

    /*
     * Both dimensions come from the text. LVGL grows a LV_SIZE_CONTENT object
     * to fit its children, and the label below wraps at a maximum width, so a
     * short reply gets a small bubble and a long one gets a tall bubble —
     * without either being hard-coded.
     */
    lv_obj_set_size(s_bubble, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_min_width(s_bubble, BUDDY_SPEECH_BUBBLE_MIN_WIDTH, LV_PART_MAIN);
    lv_obj_set_style_max_width(s_bubble, BUDDY_SPEECH_BUBBLE_MAX_WIDTH, LV_PART_MAIN);
    /*
     * Deliberately NO max-height style here. A capped bubble reports the capped
     * height, so fit_bubble_to_text() could never tell that the text had
     * overflowed — it would just quietly clip. The height limit is enforced by
     * that function instead, which can actually do something about it.
     */
    lv_obj_align(s_bubble, LV_ALIGN_BOTTOM_RIGHT,
                 -BUDDY_SCREEN_EDGE_MARGIN, -BUDDY_SCREEN_EDGE_MARGIN);

    lv_obj_set_style_bg_color(s_bubble, FACE_COLOR_SURFACE, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_bubble, LV_OPA_COVER, LV_PART_MAIN);
    /*
     * Radius steps: chips 12, caption 14, screen corners are the panel's own.
     * The hairline border is what separates the sheet from the background in
     * the day theme, where the two warm tones are deliberately close.
     */
    lv_obj_set_style_radius(s_bubble, 14, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_bubble, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(s_bubble, FACE_COLOR_SURFACE_BORDER, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_bubble, BUDDY_SPEECH_BUBBLE_PADDING, LV_PART_MAIN);

    s_label = lv_label_create(s_bubble);
    /*
     * Width from the text, capped — which is what makes the label wrap at the
     * cap for long text and stay narrow for short text. A percentage width
     * here would force every bubble to full width, which is the bulky look
     * this replaces. fit_bubble_to_text() sets these again per caption; the
     * values here are just a sane starting state.
     */
    lv_obj_set_width(s_label, LV_SIZE_CONTENT);
    lv_obj_set_style_max_width(s_label, CAPTION_MAX_WIDTH, LV_PART_MAIN);
    lv_label_set_long_mode(s_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(s_label, s_caption_fonts[0], LV_PART_MAIN);
    lv_obj_set_style_text_color(s_label, FACE_COLOR_TEXT, LV_PART_MAIN);
    lv_obj_set_style_text_align(s_label, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);
    lv_obj_set_style_text_line_space(s_label, 2, LV_PART_MAIN);
    lv_label_set_text(s_label, "");
    lv_obj_align(s_label, LV_ALIGN_TOP_LEFT, 0, 0);

    lv_obj_add_flag(s_bubble, LV_OBJ_FLAG_HIDDEN);

    ESP_LOGI(TAG, "Speech bubble created (%u caption sizes)", (unsigned)CAPTION_FONT_COUNT);

#if !defined(LV_FONT_MONTSERRAT_12) || !LV_FONT_MONTSERRAT_12
    ESP_LOGW(TAG, "No 12 px font in this build, so a reply over ~400 characters "
                  "would run off the top. Add CONFIG_LV_FONT_MONTSERRAT_12=y to "
                  "sdkconfig to close that gap.");
#endif

    return ESP_OK;
}

void ui_speech_bubble_show(ui_speech_source_t source, const char *text, uint32_t auto_hide_ms)
{
    if (s_bubble == NULL || text == NULL) {
        return;
    }

    /* Colour the text by who is speaking. */
    switch (source) {
    case UI_SPEECH_FROM_BUDDY:
        lv_obj_set_style_text_color(s_label, FACE_COLOR_ESPRESSIF_RED, LV_PART_MAIN);
        break;
    case UI_SPEECH_FROM_USER:
        lv_obj_set_style_text_color(s_label, FACE_COLOR_TEXT, LV_PART_MAIN);
        break;
    case UI_SPEECH_STATUS:
    default:
        lv_obj_set_style_text_color(s_label, FACE_COLOR_TEXT_MUTED, LV_PART_MAIN);
        break;
    }

    /*
     * Size the bubble around this particular caption. The call settles the
     * layout as it goes, so the height it hands back is the real one — reading
     * it straight off the object without that would still give the PREVIOUS
     * caption's height, and the face would lift by the wrong amount for a
     * frame. That shows up as a twitch.
     */
    const lv_coord_t bubble_height = fit_bubble_to_text(text);

    /*
     * Fade in only when the bubble was away. Swapping one caption for the
     * next while it is already up just changes the words, without a flicker.
     */
    const bool was_visible = s_visible;
    lv_obj_clear_flag(s_bubble, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_bubble);
    if (!was_visible) {
        caption_fade(0, 255, CAPTION_ENTER_MS, NULL);
    } else {
        caption_fade(255, 255, 0, NULL);   /* Cancel a half-finished exit. */
    }

    /* Lift the face by an amount that suits the bubble actually on screen. */
    int16_t lift = (int16_t)(bubble_height / BUDDY_FACE_LIFT_DIVISOR);
    if (lift > BUDDY_FACE_LIFT_MAX) {
        lift = BUDDY_FACE_LIFT_MAX;
    }
    face_view_set_vertical_shift(-lift, FACE_SLIDE_MS);
    s_visible = true;

    /* Restart the auto-hide countdown. */
    if (s_hide_timer != NULL) {
        lv_timer_del(s_hide_timer);
        s_hide_timer = NULL;
    }
    if (auto_hide_ms > 0) {
        s_hide_timer = lv_timer_create(auto_hide_timer_cb, auto_hide_ms, NULL);
        lv_timer_set_repeat_count(s_hide_timer, 1);
    }
}

void ui_speech_bubble_hide(void)
{
    if (s_bubble == NULL) {
        return;
    }

    if (s_hide_timer != NULL) {
        lv_timer_del(s_hide_timer);
        s_hide_timer = NULL;
    }

    if (s_visible) {
        s_visible = false;
        face_view_set_vertical_shift(0, FACE_SLIDE_MS);
        caption_fade(255, 0, CAPTION_EXIT_MS, caption_exit_done_cb);
    } else {
        lv_obj_add_flag(s_bubble, LV_OBJ_FLAG_HIDDEN);
    }
}

bool ui_speech_bubble_is_visible(void)
{
    return s_visible;
}
