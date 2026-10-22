/*
 * face_emotions.h — The catalogue of expressions and idle antics.
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 *
 * An EMOTION is a single pose the face can rest in (happy, sleepy, ...).
 *
 * An ANTIC is a short scripted sequence of poses — the thing the buddy does
 * spontaneously when it has been idle for a while, or when you tap the screen.
 * Think of the Las Vegas Sphere idly blinking and glancing around: a handful of
 * small, well-timed moves that make it feel alive rather than frozen.
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "face_pose.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Every expression the buddy knows how to wear.
 */
typedef enum {
    FACE_EMOTION_NEUTRAL = 0,   /*!< Calm, gently smiling default state        */
    FACE_EMOTION_HAPPY,         /*!< Squinting "^ ^" eyes, big smile, blush     */
    FACE_EMOTION_EXCITED,       /*!< Huge round eyes, wide open grin            */
    FACE_EMOTION_CURIOUS,       /*!< One brow up, eyes glancing to the side     */
    FACE_EMOTION_SURPRISED,     /*!< Brows way up, eyes wide, small round mouth */
    FACE_EMOTION_SAD,           /*!< Inner brows up, downturned mouth           */
    FACE_EMOTION_ANNOYED,       /*!< Inner brows down, flat narrow mouth        */
    FACE_EMOTION_SLEEPY,        /*!< Heavy half-lids, tiny mouth                */
    FACE_EMOTION_YAWN,          /*!< Sleepy eyes with a big open mouth          */
    FACE_EMOTION_ASLEEP,        /*!< Eyes shut, no brows                        */
    FACE_EMOTION_WINK,          /*!< Left eye shut, cheeky smile                */
    FACE_EMOTION_LOVE,          /*!< Squished happy eyes, blush at full         */
    FACE_EMOTION_CONFUSED,      /*!< Lopsided brows, wavy little mouth          */
    FACE_EMOTION_DIZZY,         /*!< Tiny eyes, no brows — used for errors      */
    FACE_EMOTION_MISCHIEF,      /*!< Half-lidded sly eyes, one brow up, smirk   */
    FACE_EMOTION_PROUD,         /*!< Eyes shut in arcs, chin up, pleased grin   */
    FACE_EMOTION_BORED,         /*!< Heavy lids, flat mouth, staring off sideways */
    FACE_EMOTION_SMUG,          /*!< Lids low, one brow cocked, lopsided smirk  */
    FACE_EMOTION_SHOCKED,       /*!< Bigger than surprised: eyes huge, jaw open */
    FACE_EMOTION_DETERMINED,    /*!< Narrowed eyes, brows down, firm flat mouth */
    FACE_EMOTION_GIGGLY,        /*!< Arc eyes, open grin, full blush, bouncing  */
    FACE_EMOTION_SUSPICIOUS,    /*!< Heavy lids, uneven brows, side-eye         */
    FACE_EMOTION_RELIEVED,      /*!< Eyes softly shut, small smile, brows high  */
    FACE_EMOTION_AMAZED,        /*!< Huge round eyes, open smile, blush         */
    FACE_EMOTION_NERVOUS,       /*!< Wide eyes, tiny mouth, inner brows up      */
    FACE_EMOTION_PLEADING,      /*!< Huge soft eyes, brows begging, tiny frown  */
    FACE_EMOTION_TRIUMPHANT,    /*!< Arc eyes, enormous open grin, chin lifted  */
    FACE_EMOTION_UNIMPRESSED,   /*!< Flat lids, one brow up, dead level mouth   */
    FACE_EMOTION_SCRUNCH,       /*!< Eyes squeezed shut, nose-wrinkling scowl   */
    FACE_EMOTION_SHY,           /*!< Looking down and away, small smile, blush  */
    FACE_EMOTION_PANIC,         /*!< Eyes enormous, mouth wide, whole face jittering */
    FACE_EMOTION_DISGUSTED,     /*!< Lopsided brows, lids low, mouth curled down */
    FACE_EMOTION_SINGING,       /*!< Eyes shut in bliss, mouth a big round O    */
    /* Added in v3.5. */
    FACE_EMOTION_STARRY,        /*!< Enormous shining eyes, blush, open smile   */
    FACE_EMOTION_SCEPTICAL,     /*!< One brow way up, lids low, mouth to one side */
    FACE_EMOTION_SHEEPISH,      /*!< Looking away, small awkward smile, blush   */
    FACE_EMOTION_HOPEFUL,       /*!< Brows lifted together, eyes up, soft smile */
    FACE_EMOTION_THOUGHTFUL,    /*!< Lids half down, gaze off to one side, flat mouth */
    FACE_EMOTION_CHEEKY,        /*!< One eye squeezed, wide lopsided grin       */
    FACE_EMOTION_ALARMED,       /*!< Eyes wide and high, brows up, mouth small and tight */
    FACE_EMOTION_COSY,          /*!< Eyes softly shut in arcs, warm blush, tiny smile */
    FACE_EMOTION_DREAMY,        /*!< Half-lidded, looking up and away, faint smile */
    FACE_EMOTION_FOCUSED,       /*!< Narrow eyes locked forward, brows level    */
    FACE_EMOTION_PLAYFUL,       /*!< Wide grin, one brow up, head tilted by an offset */
    FACE_EMOTION_EXHAUSTED,     /*!< Lids heavy, brows sagging, mouth slack     */

    FACE_EMOTION_LISTENING,     /*!< Alert, brows raised, mouth a small circle  */
    FACE_EMOTION_THINKING,      /*!< Eyes rolled up and to one side             */
    FACE_EMOTION_SPEAKING,      /*!< Friendly base pose the lip-sync animates   */
    FACE_EMOTION_COUNT,
} face_emotion_t;

/**
 * @brief One step of a scripted antic.
 */
typedef struct {
    face_emotion_t emotion;       /*!< Expression to move into for this step   */
    int16_t        look_x;        /*!< Eye direction override, -100..100       */
    int16_t        look_y;        /*!< Eye direction override, -100..100       */
    int16_t        face_offset_x; /*!< Whole-face nudge, in pixels             */
    int16_t        face_offset_y; /*!< Whole-face nudge, in pixels             */
    uint16_t       transition_ms; /*!< Time spent gliding into this step       */
    uint16_t       hold_ms;       /*!< Time spent sitting in it afterwards     */
} face_antic_step_t;

/**
 * @brief A named sequence of steps.
 */
typedef struct {
    const char              *name;        /*!< Shown in logs, handy for debug  */
    const face_antic_step_t *steps;
    uint8_t                  step_count;
} face_antic_t;

/**
 * @brief Look up the resting pose for an emotion.
 *
 * @param emotion  Which expression to fetch. Out-of-range values return neutral.
 * @return Pointer to a constant pose. Never NULL.
 */
const face_pose_t *face_emotion_get_pose(face_emotion_t emotion);

/**
 * @brief Human-readable name of an emotion, e.g. "happy". Never NULL.
 */
const char *face_emotion_get_name(face_emotion_t emotion);

/**
 * @brief Parse an emotion name back into its enum value.
 *
 * Matching is case-insensitive. Used to understand the `[happy]` tag the cloud
 * brain is asked to put at the front of its replies.
 *
 * @param name      Text to match, need not be NUL-terminated at the emotion end.
 * @param name_len  Number of characters of @p name to consider.
 * @param out       Receives the matched emotion.
 * @return true when a match was found.
 */
bool face_emotion_from_name(const char *name, size_t name_len, face_emotion_t *out);

/**
 * @brief How many idle antics are in the catalogue.
 */
uint8_t face_antic_get_count(void);

/**
 * @brief Fetch an antic by index (0 .. face_antic_get_count()-1).
 *
 * @return Pointer to a constant antic, or NULL when the index is out of range.
 */
const face_antic_t *face_antic_get(uint8_t index);

#ifdef __cplusplus
}
#endif
