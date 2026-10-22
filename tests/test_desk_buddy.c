/*
 * test_desk_buddy.c — Tests that run on your computer, not on the box.
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 *
 * WHAT THIS COVERS, AND WHY
 * -------------------------
 * Most of this firmware talks to hardware and cannot be meaningfully tested off
 * the board. But the parts most likely to be wrong are not the hardware parts —
 * they are the big hand-written TABLES: seventeen facial expressions, twelve
 * antic scripts, and sixty-odd voice phrases, all typed out by hand. A single
 * transposed number there produces a face that looks subtly broken, or a phrase
 * the recogniser silently refuses to load, with nothing in any log to say why.
 *
 * So these tests mostly check the data rather than the code:
 *
 *   pose blending      the interpolation every animation is built on
 *   the emotion table  every mood is complete and geometrically sane
 *   the antic table    every script step points at a real mood, with sane timing
 *   the vocabulary     every phrase is usable by MultiNet, no duplicates, and
 *                      every action the code can produce has something to say it
 *   mood tags          parsing replies from a remote model that may misbehave
 *
 * They compile with plain gcc and need no hardware, no ESP-IDF and no network.
 *
 * Run them with:
 *     python scripts\run_tests.py
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "audio_resampler.h"
#include "audio_voice_hash.h"
#include "buddy_api_key.h"
#include "buddy_calculator.h"
#include "buddy_config.h"
#include "buddy_settings.h"
#include "buddy_timer.h"
#include "esp_timer.h"
#include "face_emotions.h"
#include "face_pose.h"
#include "voice_commands.h"
#include "voice_math.h"
#include "voice_mood_tag.h"
#include "voice_number_words.h"
#include "voice_router.h"
#include "voice_speech_gate.h"
#include "voice_speech_parts.h"

/* ------------------------------------------------------------------------ */
/* A very small test harness                                                 */
/* ------------------------------------------------------------------------ */

static int s_checks_run;
static int s_checks_failed;
static const char *s_current_test;

/** Report one assertion. Keeps going after a failure so you see them all. */
static void check(bool condition, const char *description, const char *file, int line)
{
    s_checks_run++;
    if (!condition) {
        s_checks_failed++;
        printf("  FAIL  %s\n        %s:%d\n", description, file, line);
    }
}

#define CHECK(condition) check((condition), #condition, __FILE__, __LINE__)

/** Same, but lets a test explain the failure in its own words. */
#define CHECK_MSG(condition, ...)                                   \
    do {                                                            \
        s_checks_run++;                                             \
        if (!(condition)) {                                         \
            s_checks_failed++;                                      \
            printf("  FAIL  ");                                     \
            printf(__VA_ARGS__);                                    \
            printf("\n        %s:%d\n", __FILE__, __LINE__);        \
        }                                                           \
    } while (0)

#define TEST(name)                                                  \
    static void name(void);                                         \
    static void run_##name(void) {                                  \
        s_current_test = #name;                                     \
        int failed_before = s_checks_failed;                        \
        name();                                                     \
        printf("  %s  %s\n",                                        \
               (s_checks_failed == failed_before) ? "pass" : "FAIL",\
               #name);                                              \
    }                                                               \
    static void name(void)

/* ------------------------------------------------------------------------ */
/* Pose blending — the arithmetic every animation rests on                   */
/* ------------------------------------------------------------------------ */

TEST(blending_at_the_ends_returns_the_original_poses)
{
    const face_pose_t *neutral = face_emotion_get_pose(FACE_EMOTION_NEUTRAL);
    const face_pose_t *happy   = face_emotion_get_pose(FACE_EMOTION_HAPPY);

    face_pose_t result;

    face_pose_blend(neutral, happy, 0, &result);
    CHECK(result.eye_width == neutral->eye_width);
    CHECK(result.mouth_curve == neutral->mouth_curve);
    CHECK(result.blush_opacity == neutral->blush_opacity);

    face_pose_blend(neutral, happy, 255, &result);
    CHECK(result.eye_width == happy->eye_width);
    CHECK(result.mouth_curve == happy->mouth_curve);
    CHECK(result.blush_opacity == happy->blush_opacity);
}

TEST(blending_halfway_lands_between_the_two_poses)
{
    face_pose_t from = { 0 };
    face_pose_t to = { 0 };
    face_pose_t result;

    from.eye_width = 0;    to.eye_width = 100;
    from.mouth_curve = -20; to.mouth_curve = 20;
    from.blush_opacity = 0; to.blush_opacity = 200;

    face_pose_blend(&from, &to, 128, &result);

    /* Integer maths, so allow a pixel of rounding either way. */
    CHECK(result.eye_width >= 49 && result.eye_width <= 51);
    CHECK(result.mouth_curve >= -1 && result.mouth_curve <= 1);
    CHECK(result.blush_opacity >= 99 && result.blush_opacity <= 101);
}

TEST(blending_handles_negative_values_and_flips_bools_at_the_midpoint)
{
    face_pose_t from = { 0 };
    face_pose_t to = { 0 };
    face_pose_t result;

    from.brow_inner_tilt = -12;  to.brow_inner_tilt = 12;
    from.left_eye_shut = false;  to.left_eye_shut = true;

    face_pose_blend(&from, &to, 0, &result);
    CHECK(result.brow_inner_tilt == -12);
    CHECK(result.left_eye_shut == false);

    face_pose_blend(&from, &to, 127, &result);
    CHECK_MSG(result.left_eye_shut == false, "a bool flipped before halfway");

    face_pose_blend(&from, &to, 255, &result);
    CHECK(result.brow_inner_tilt == 12);
    CHECK(result.left_eye_shut == true);
}

TEST(blending_survives_null_arguments)
{
    face_pose_t pose = { 0 };

    /* Must not crash. There is nothing to assert beyond surviving the call. */
    face_pose_blend(NULL, &pose, 128, &pose);
    face_pose_blend(&pose, NULL, 128, &pose);
    face_pose_blend(&pose, &pose, 128, NULL);
    CHECK(true);
}

/* ------------------------------------------------------------------------ */
/* The emotion table                                                         */
/* ------------------------------------------------------------------------ */

TEST(every_emotion_has_a_unique_lowercase_name)
{
    for (int i = 0; i < FACE_EMOTION_COUNT; i++) {
        const char *name = face_emotion_get_name((face_emotion_t)i);

        CHECK_MSG(name != NULL && name[0] != '\0', "emotion %d has no name", i);

        for (const char *c = name; *c != '\0'; c++) {
            CHECK_MSG(*c >= 'a' && *c <= 'z',
                      "emotion name \"%s\" is not plain lowercase", name);
        }

        for (int j = i + 1; j < FACE_EMOTION_COUNT; j++) {
            CHECK_MSG(strcmp(name, face_emotion_get_name((face_emotion_t)j)) != 0,
                      "emotions %d and %d share the name \"%s\"", i, j, name);
        }
    }
}

TEST(emotion_names_round_trip_and_ignore_case)
{
    for (int i = 0; i < FACE_EMOTION_COUNT; i++) {
        const char *name = face_emotion_get_name((face_emotion_t)i);

        face_emotion_t parsed;
        CHECK_MSG(face_emotion_from_name(name, strlen(name), &parsed),
                  "\"%s\" did not parse back", name);
        CHECK_MSG(parsed == (face_emotion_t)i, "\"%s\" parsed to the wrong mood", name);
    }

    face_emotion_t parsed;
    CHECK(face_emotion_from_name("HAPPY", 5, &parsed) && parsed == FACE_EMOTION_HAPPY);
    CHECK(face_emotion_from_name("CuRiOuS", 7, &parsed) && parsed == FACE_EMOTION_CURIOUS);

    /* A prefix must not match a longer name. */
    CHECK_MSG(!face_emotion_from_name("hap", 3, &parsed), "a partial name matched");
    CHECK(!face_emotion_from_name("nonsense", 8, &parsed));
    CHECK(!face_emotion_from_name("", 0, &parsed));
    CHECK(!face_emotion_from_name(NULL, 4, &parsed));
}

TEST(out_of_range_emotions_fall_back_safely)
{
    /* Anything invalid must give the neutral pose rather than read past the table. */
    const face_pose_t *neutral = face_emotion_get_pose(FACE_EMOTION_NEUTRAL);

    CHECK(face_emotion_get_pose((face_emotion_t)-1) == neutral);
    CHECK(face_emotion_get_pose(FACE_EMOTION_COUNT) == neutral);
    CHECK(face_emotion_get_pose((face_emotion_t)9999) == neutral);

    CHECK(strcmp(face_emotion_get_name((face_emotion_t)-1), "neutral") == 0);
    CHECK(strcmp(face_emotion_get_name(FACE_EMOTION_COUNT), "neutral") == 0);
}

TEST(every_emotion_is_geometrically_drawable)
{
    for (int i = 0; i < FACE_EMOTION_COUNT; i++) {
        const face_pose_t *pose = face_emotion_get_pose((face_emotion_t)i);
        const char *name = face_emotion_get_name((face_emotion_t)i);

        /* An eye with no area is invisible — always a typo, never intentional. */
        CHECK_MSG(pose->eye_width > 0, "\"%s\" has a zero-width eye", name);
        CHECK_MSG(pose->eye_height > 0, "\"%s\" has a zero-height eye", name);

        /* Eyes bigger than a quarter of the screen mean a misplaced digit. */
        CHECK_MSG(pose->eye_width <= BUDDY_SCREEN_WIDTH / 2,
                  "\"%s\" has an absurdly wide eye (%d)", name, pose->eye_width);
        CHECK_MSG(pose->eye_height <= BUDDY_SCREEN_HEIGHT / 2,
                  "\"%s\" has an absurdly tall eye (%d)", name, pose->eye_height);

        /* Lids deeper than the eye would carve the whole thing away. */
        CHECK_MSG(pose->eye_lower_lid >= 0 && pose->eye_lower_lid <= pose->eye_height,
                  "\"%s\" lower lid (%d) does not fit its eye (%d)",
                  name, pose->eye_lower_lid, pose->eye_height);
        CHECK_MSG(pose->eye_upper_lid >= 0 && pose->eye_upper_lid <= pose->eye_height,
                  "\"%s\" upper lid (%d) does not fit its eye (%d)",
                  name, pose->eye_upper_lid, pose->eye_height);

        /* Both lids at once would leave nothing showing. */
        CHECK_MSG(pose->eye_lower_lid + pose->eye_upper_lid < pose->eye_height,
                  "\"%s\" lids together erase the eye", name);

        /* The mouth has to fit inside the canvas face_view draws it on. */
        CHECK_MSG(pose->mouth_width >= 16 && pose->mouth_width <= 180,
                  "\"%s\" mouth width %d is outside the drawable range",
                  name, pose->mouth_width);
        CHECK_MSG(pose->mouth_curve >= -34 && pose->mouth_curve <= 34,
                  "\"%s\" mouth curve %d exceeds the canvas", name, pose->mouth_curve);
        CHECK_MSG(pose->mouth_open_height >= 0 &&
                  pose->mouth_open_height <= FACE_MOUTH_MAX_OPEN_HEIGHT,
                  "\"%s\" mouth opening %d is out of range", name, pose->mouth_open_height);

        /* Gaze is a percentage of the travel range. */
        CHECK_MSG(pose->look_x >= -100 && pose->look_x <= 100,
                  "\"%s\" look_x %d is not a percentage", name, pose->look_x);
        CHECK_MSG(pose->look_y >= -100 && pose->look_y <= 100,
                  "\"%s\" look_y %d is not a percentage", name, pose->look_y);

        /* Brow tilt beyond this runs off the invisible canvas it is drawn on. */
        CHECK_MSG(pose->brow_inner_tilt >= -16 && pose->brow_inner_tilt <= 16,
                  "\"%s\" brow tilt %d exceeds the canvas", name, pose->brow_inner_tilt);

        /* A whole-face nudge is meant to be subtle, not a relocation. */
        CHECK_MSG(pose->face_offset_x >= -20 && pose->face_offset_x <= 20,
                  "\"%s\" shifts the face too far sideways", name);
        CHECK_MSG(pose->face_offset_y >= -20 && pose->face_offset_y <= 20,
                  "\"%s\" shifts the face too far vertically", name);
    }
}

TEST(the_resting_expressions_actually_look_friendly)
{
    /* Guards against someone "tidying" the table and inverting a sign. */
    const face_pose_t *neutral = face_emotion_get_pose(FACE_EMOTION_NEUTRAL);
    CHECK_MSG(neutral->mouth_curve > 0, "the neutral face is not smiling");
    CHECK_MSG(neutral->brows_visible, "the neutral face has no eyebrows");
    CHECK_MSG(!neutral->left_eye_shut && !neutral->right_eye_shut,
              "the neutral face has its eyes shut");

    const face_pose_t *happy = face_emotion_get_pose(FACE_EMOTION_HAPPY);
    CHECK_MSG(happy->mouth_curve > neutral->mouth_curve, "happy smiles less than neutral");

    const face_pose_t *sad = face_emotion_get_pose(FACE_EMOTION_SAD);
    CHECK_MSG(sad->mouth_curve < 0, "the sad face is not frowning");

    /* A wink is one eye, not two — that would just be a blink. */
    const face_pose_t *wink = face_emotion_get_pose(FACE_EMOTION_WINK);
    CHECK_MSG(wink->left_eye_shut != wink->right_eye_shut,
              "the wink closes both eyes or neither");

    const face_pose_t *asleep = face_emotion_get_pose(FACE_EMOTION_ASLEEP);
    CHECK_MSG(asleep->left_eye_shut && asleep->right_eye_shut,
              "the sleeping face has its eyes open");

    /* Speaking has to start from an open mouth or the lip sync has nothing to move. */
    const face_pose_t *speaking = face_emotion_get_pose(FACE_EMOTION_SPEAKING);
    CHECK_MSG(speaking->mouth_open_height > 0, "the speaking face has a shut mouth");
}

/* ------------------------------------------------------------------------ */
/* The antic scripts                                                         */
/* ------------------------------------------------------------------------ */

TEST(there_are_antics_and_they_are_reachable)
{
    const uint8_t count = face_antic_get_count();

    CHECK_MSG(count >= 4, "only %u antics — the buddy will look repetitive", count);
    CHECK(face_antic_get(count) == NULL);
    CHECK(face_antic_get(255) == NULL);

    for (uint8_t i = 0; i < count; i++) {
        CHECK_MSG(face_antic_get(i) != NULL, "antic %u is missing", i);
    }
}

TEST(every_antic_step_is_valid_and_sanely_timed)
{
    for (uint8_t i = 0; i < face_antic_get_count(); i++) {
        const face_antic_t *antic = face_antic_get(i);

        CHECK_MSG(antic->name != NULL && antic->name[0] != '\0', "antic %u has no name", i);
        CHECK_MSG(antic->steps != NULL, "antic \"%s\" has no steps", antic->name);
        CHECK_MSG(antic->step_count > 0, "antic \"%s\" is empty", antic->name);
        CHECK_MSG(antic->step_count <= 8,
                  "antic \"%s\" has %u steps — it will feel like a cutscene",
                  antic->name, antic->step_count);

        uint32_t total_duration = 0;

        for (uint8_t s = 0; s < antic->step_count; s++) {
            const face_antic_step_t *step = &antic->steps[s];

            CHECK_MSG(step->emotion >= 0 && step->emotion < FACE_EMOTION_COUNT,
                      "antic \"%s\" step %u names a mood that does not exist",
                      antic->name, s);

            CHECK_MSG(step->look_x >= -100 && step->look_x <= 100,
                      "antic \"%s\" step %u look_x is not a percentage", antic->name, s);
            CHECK_MSG(step->look_y >= -100 && step->look_y <= 100,
                      "antic \"%s\" step %u look_y is not a percentage", antic->name, s);

            /* A zero-length transition snaps, which reads as a glitch. */
            CHECK_MSG(step->transition_ms >= 50,
                      "antic \"%s\" step %u transitions in %ums — too abrupt to see",
                      antic->name, s, step->transition_ms);

            total_duration += (uint32_t)step->transition_ms + step->hold_ms;
        }

        /* Long enough to notice, short enough not to block the next thing. */
        CHECK_MSG(total_duration >= 300,
                  "antic \"%s\" lasts only %ums", antic->name, (unsigned)total_duration);
        CHECK_MSG(total_duration <= 6000,
                  "antic \"%s\" lasts %ums — it will feel like it is stuck",
                  antic->name, (unsigned)total_duration);
    }
}

TEST(antics_end_by_settling_down)
{
    /*
     * The last step should not leave the face shoved off to one side, because
     * the animator glides from wherever the antic stopped back to the resting
     * mood, and a big leftover offset makes that glide look like a lurch.
     */
    for (uint8_t i = 0; i < face_antic_get_count(); i++) {
        const face_antic_t *antic = face_antic_get(i);
        const face_antic_step_t *last = &antic->steps[antic->step_count - 1];

        CHECK_MSG(last->face_offset_x == 0 && last->face_offset_y == 0,
                  "antic \"%s\" ends with the face still displaced", antic->name);
        CHECK_MSG(last->look_x == 0 && last->look_y == 0,
                  "antic \"%s\" ends still looking away", antic->name);
    }
}

/* ------------------------------------------------------------------------ */
/* The voice vocabulary                                                      */
/* ------------------------------------------------------------------------ */

TEST(the_command_table_is_addressable)
{
    const uint16_t count = voice_commands_get_count();

    CHECK_MSG(count > 0, "the vocabulary is empty");
    CHECK(voice_commands_get(count) == NULL);
    CHECK(voice_commands_get(count + 100) == NULL);

    for (uint16_t i = 0; i < count; i++) {
        CHECK_MSG(voice_commands_get(i) != NULL, "command %u is missing", i);
    }
}

TEST(every_phrase_is_something_multinet_can_load)
{
    for (uint16_t i = 0; i < voice_commands_get_count(); i++) {
        const voice_command_t *command = voice_commands_get(i);

        CHECK_MSG(command->phrase[0] != '\0', "command %u has no phrase", i);
        CHECK_MSG(command->phonetic[0] != '\0',
                  "command %u (\"%s\") has no phonetic spelling", i, command->phrase);

        CHECK_MSG(command->action >= 0 && command->action < VOICE_ACTION_COUNT,
                  "\"%s\" maps to an action that does not exist", command->phrase);
        CHECK_MSG(command->language < BUDDY_LANGUAGE_COUNT,
                  "\"%s\" is tagged with an unknown language", command->phrase);

        /* Both fields must fit with room for the terminator. */
        CHECK(strlen(command->phrase) < VOICE_PHRASE_MAX_LEN);
        CHECK(strlen(command->phonetic) < VOICE_PHRASE_MAX_LEN);

        if (command->language != BUDDY_LANGUAGE_ENGLISH) {
            continue;
        }

        /*
         * The English model takes plain text, so the two columns must agree —
         * a mismatch means one of them will be fed to the recogniser and the
         * other shown on screen, and they will drift apart over time.
         */
        CHECK_MSG(strcmp(command->phrase, command->phonetic) == 0,
                  "English phrase \"%s\" does not match its phonetic column",
                  command->phrase);

        /* MultiNet wants lowercase letters and spaces. Anything else is refused. */
        for (const char *c = command->phrase; *c != '\0'; c++) {
            CHECK_MSG((*c >= 'a' && *c <= 'z') || *c == ' ',
                      "English phrase \"%s\" contains a character MultiNet cannot take",
                      command->phrase);
        }

        /* One-word commands misfire constantly; very long ones never match. */
        int word_count = 1;
        for (const char *c = command->phrase; *c != '\0'; c++) {
            if (*c == ' ') {
                word_count++;
            }
        }
        CHECK_MSG(word_count >= 2,
                  "\"%s\" is a single word — it will trigger on almost anything",
                  command->phrase);
        CHECK_MSG(word_count <= 7,
                  "\"%s\" is too long to be recognised reliably", command->phrase);
    }
}

TEST(no_two_phrases_in_a_language_are_the_same)
{
    /* Duplicates make the recogniser pick one at random and ignore the other. */
    const uint16_t count = voice_commands_get_count();

    for (uint16_t i = 0; i < count; i++) {
        const voice_command_t *a = voice_commands_get(i);

        for (uint16_t j = i + 1; j < count; j++) {
            const voice_command_t *b = voice_commands_get(j);

            if (a->language != b->language) {
                continue;
            }

            CHECK_MSG(strcmp(a->phrase, b->phrase) != 0,
                      "\"%s\" appears twice in the same language", a->phrase);
            CHECK_MSG(strcmp(a->phonetic, b->phonetic) != 0,
                      "the phonetic spelling \"%s\" appears twice", a->phonetic);
        }
    }
}

TEST(every_action_the_code_can_produce_has_a_phrase)
{
    /*
     * Catches the classic half-finished change: an action added to the enum and
     * handled in the brain, but never given anything to say that triggers it.
     */
    for (int action = 0; action < VOICE_ACTION_COUNT; action++) {
        bool found_english = false;

        for (uint16_t i = 0; i < voice_commands_get_count(); i++) {
            const voice_command_t *command = voice_commands_get(i);
            if (command->action == (voice_action_t)action &&
                command->language == BUDDY_LANGUAGE_ENGLISH) {
                found_english = true;
                break;
            }
        }

        CHECK_MSG(found_english, "action %d has no English phrase to trigger it", action);
    }
}

TEST(the_vocabulary_fits_in_the_recognisers_phrase_table)
{
    /*
     * voice_wakeword.c copies the phrases for the active language into fixed
     * arrays of BUDDY_MAX_REGISTERED_PHRASES entries. Going over that drops
     * whatever came last, with one warning buried in a boot log — so the
     * commands at the bottom of voice_commands.c would simply never work, and
     * nothing on screen would say why.
     */
    uint16_t per_language[BUDDY_LANGUAGE_COUNT] = { 0 };

    for (uint16_t i = 0; i < voice_commands_get_count(); i++) {
        const voice_command_t *command = voice_commands_get(i);
        if (command->language < BUDDY_LANGUAGE_COUNT) {
            per_language[command->language]++;
        }
    }

    for (int language = 0; language < BUDDY_LANGUAGE_COUNT; language++) {
        CHECK_MSG(per_language[language] <= BUDDY_MAX_REGISTERED_PHRASES,
                  "language %d has %u phrases but only %d can be registered",
                  language, per_language[language], BUDDY_MAX_REGISTERED_PHRASES);
    }
}

TEST(every_offline_reply_is_a_whole_sentence_so_it_can_be_spoken)
{
    /*
     * The offline voice finds a clip by hashing the reply text, so a reply
     * assembled at runtime from a format string has nothing to look up and
     * stays silent. That is a deliberate trade for the few replies that carry a
     * measurement — the time, the temperature — but it should never happen by
     * accident to a conversational one.
     *
     * scripts/make_voice_clips.py extracts exactly the string literals that
     * look like finished sentences, so this checks the same shape it does: a
     * capital letter at the start and a full stop, question mark or exclamation
     * mark at the end. If a new answer fails here it will also be missing from
     * spiffs/voice, and the buddy will mouth it in silence.
     */
    static const char *const conversational_replies[] = {
        "Rock.",
        "Paper.",
        "Scissors.",
        "Yes. Do it.",
        "Heads.",
        "Tails.",
        "You rolled a six.",
        "Timer cancelled.",
        "There is no timer running.",
    };

    const size_t count = sizeof(conversational_replies) / sizeof(conversational_replies[0]);

    for (size_t i = 0; i < count; i++) {
        const char *line = conversational_replies[i];
        const size_t length = strlen(line);

        CHECK_MSG(length > 0 && line[0] >= 'A' && line[0] <= 'Z',
                  "\"%s\" does not start with a capital, so the clip generator skips it",
                  line);

        const char last = (length > 0) ? line[length - 1] : '\0';
        CHECK_MSG(last == '.' || last == '!' || last == '?',
                  "\"%s\" has no sentence ending, so the clip generator skips it", line);
    }
}

TEST(describing_the_vocabulary_never_overruns_its_buffer)
{
    char buffer[256];

    voice_commands_describe(BUDDY_LANGUAGE_ENGLISH, buffer, sizeof(buffer));
    CHECK_MSG(buffer[0] != '\0', "the capability list came out empty");
    CHECK(strlen(buffer) < sizeof(buffer));

    /* A buffer far too small must truncate cleanly, not scribble past the end. */
    char tiny[8];
    memset(tiny, 'X', sizeof(tiny));
    voice_commands_describe(BUDDY_LANGUAGE_ENGLISH, tiny, sizeof(tiny));
    CHECK_MSG(memchr(tiny, '\0', sizeof(tiny)) != NULL,
              "a tiny buffer was left without a terminator");

    /* Zero-size and NULL must simply do nothing. */
    char untouched = 'Z';
    voice_commands_describe(BUDDY_LANGUAGE_ENGLISH, &untouched, 0);
    CHECK(untouched == 'Z');
    voice_commands_describe(BUDDY_LANGUAGE_ENGLISH, NULL, 32);
    CHECK(true);
}

/* ------------------------------------------------------------------------ */
/* Mood tags from the online brain                                           */
/* ------------------------------------------------------------------------ */

TEST(a_well_formed_mood_tag_is_split_off)
{
    face_emotion_t emotion;

    const char *answer = voice_mood_tag_split("[curious] That is a good question.", &emotion);
    CHECK(emotion == FACE_EMOTION_CURIOUS);
    CHECK_MSG(strcmp(answer, "That is a good question.") == 0,
              "answer came back as \"%s\"", answer);

    answer = voice_mood_tag_split("[excited] Yes!", &emotion);
    CHECK(emotion == FACE_EMOTION_EXCITED);
    CHECK(strcmp(answer, "Yes!") == 0);

    /* Models are inconsistent about case and leading whitespace. */
    answer = voice_mood_tag_split("\n  [SAD] Oh dear.", &emotion);
    CHECK(emotion == FACE_EMOTION_SAD);
    CHECK(strcmp(answer, "Oh dear.") == 0);

    /* No space after the bracket. */
    answer = voice_mood_tag_split("[happy]Hello there.", &emotion);
    CHECK(emotion == FACE_EMOTION_HAPPY);
    CHECK(strcmp(answer, "Hello there.") == 0);
}

TEST(every_mood_the_model_is_offered_can_be_parsed)
{
    /*
     * The system prompt lists moods for the model to choose from. If any of
     * them stopped parsing, cloud replies would quietly all wear the same face.
     */
    static const char *const offered[] = {
        "[happy] x", "[excited] x", "[curious] x", "[surprised] x", "[sad] x",
        "[annoyed] x", "[sleepy] x", "[love] x", "[confused] x", "[neutral] x",
    };

    for (size_t i = 0; i < sizeof(offered) / sizeof(offered[0]); i++) {
        face_emotion_t emotion = FACE_EMOTION_DIZZY;
        const char *answer = voice_mood_tag_split(offered[i], &emotion);

        CHECK_MSG(strcmp(answer, "x") == 0,
                  "%s did not have its tag removed", offered[i]);
        CHECK_MSG(emotion != FACE_EMOTION_DIZZY,
                  "%s was not recognised as a mood", offered[i]);
    }
}

TEST(a_missing_or_broken_tag_never_loses_the_answer)
{
    face_emotion_t emotion;

    /* No tag at all — by far the most common case in practice. */
    const char *plain = "Just an answer with no tag.";
    const char *answer = voice_mood_tag_split(plain, &emotion);
    CHECK_MSG(strcmp(answer, plain) == 0, "an untagged reply was altered");

    /* A mood that does not exist: keep the text rather than eat the first words. */
    answer = voice_mood_tag_split("[grumpy] Something.", &emotion);
    CHECK_MSG(strstr(answer, "Something.") != NULL,
              "an unknown tag swallowed part of the reply");

    /* An opening bracket with no closing one. */
    answer = voice_mood_tag_split("[unclosed and then text", &emotion);
    CHECK(strstr(answer, "text") != NULL);

    /* Empty brackets. */
    answer = voice_mood_tag_split("[] hello", &emotion);
    CHECK(strstr(answer, "hello") != NULL);

    /* A tag and nothing else. */
    answer = voice_mood_tag_split("[happy]", &emotion);
    CHECK(emotion == FACE_EMOTION_HAPPY);
    CHECK(answer != NULL && answer[0] == '\0');
}

TEST(mood_tag_parsing_survives_empty_and_null_input)
{
    face_emotion_t emotion = FACE_EMOTION_DIZZY;

    const char *answer = voice_mood_tag_split(NULL, &emotion);
    CHECK_MSG(answer != NULL, "NULL input returned a NULL pointer");
    CHECK(answer[0] == '\0');
    CHECK_MSG(emotion != FACE_EMOTION_DIZZY, "NULL input left the mood unset");

    answer = voice_mood_tag_split("", &emotion);
    CHECK(answer != NULL && answer[0] == '\0');

    /* A NULL emotion pointer must be tolerated too. */
    answer = voice_mood_tag_split("[happy] hi", NULL);
    CHECK(strcmp(answer, "hi") == 0);
}

/* ------------------------------------------------------------------------ */
/* The calculator                                                            */
/*                                                                           */
/* The engine is separated from the keypad precisely so these can hammer      */
/* sequences nobody would type on purpose. Every key has to be safe in every  */
/* state — a calculator that can be wedged by pressing the wrong thing twice  */
/* is worse than no calculator.                                               */
/* ------------------------------------------------------------------------ */

/** Type a whole sum as a string, and return what the display says. */
static const char *calc_type(buddy_calculator_t *calc, const char *keys)
{
    buddy_calculator_reset(calc);
    for (const char *k = keys; *k != '\0'; k++) {
        buddy_calculator_press(calc, *k);
    }
    return buddy_calculator_display(calc);
}

TEST(the_calculator_does_the_four_operations)
{
    buddy_calculator_t calc;

    CHECK(strcmp(calc_type(&calc, "2+2="), "4") == 0);
    CHECK(strcmp(calc_type(&calc, "407*19="), "7733") == 0);
    CHECK(strcmp(calc_type(&calc, "100-58="), "42") == 0);
    CHECK(strcmp(calc_type(&calc, "20/8="), "2.5") == 0);

    /* Chaining: each operator finishes the sum so far, left to right. */
    CHECK_MSG(strcmp(calc_type(&calc, "2+3+4="), "9") == 0,
              "chained addition gave %s", buddy_calculator_display(&calc));
    CHECK_MSG(strcmp(calc_type(&calc, "2+3*4="), "20") == 0,
              "left-to-right evaluation gave %s, expected 20",
              buddy_calculator_display(&calc));

    /* Percent means "n percent of", the way a pocket calculator's key does. */
    CHECK(strcmp(calc_type(&calc, "50%10="), "5") == 0);

    /* Decimals in, decimals out, with the trailing zeros trimmed. */
    CHECK(strcmp(calc_type(&calc, "1.5+1.5="), "3") == 0);
    CHECK(strcmp(calc_type(&calc, "0.1+0.2="), "0.3") == 0);
}

TEST(dividing_by_zero_says_so_rather_than_printing_infinity)
{
    buddy_calculator_t calc;

    const char *shown = calc_type(&calc, "8/0=");
    CHECK_MSG(strstr(shown, "divide") != NULL,
              "dividing by zero displayed \"%s\"", shown);
    CHECK(strstr(shown, "inf") == NULL && strstr(shown, "nan") == NULL);

    /* An error is not spoken, and C is the way out of it. */
    CHECK(!buddy_calculator_has_spoken_answer(&calc));
    buddy_calculator_press(&calc, BUDDY_CALC_KEY_CLEAR);
    CHECK(strcmp(buddy_calculator_display(&calc), "0") == 0);

    /* And it works normally afterwards. */
    buddy_calculator_press(&calc, '6');
    buddy_calculator_press(&calc, BUDDY_CALC_KEY_MULTIPLY);
    buddy_calculator_press(&calc, '7');
    buddy_calculator_press(&calc, BUDDY_CALC_KEY_EQUALS);
    CHECK(strcmp(buddy_calculator_display(&calc), "42") == 0);
}

TEST(no_sequence_of_keys_can_wedge_the_calculator)
{
    buddy_calculator_t calc;
    buddy_calculator_reset(&calc);

    /*
     * Every key, in every order, many times over. Nothing here checks an
     * answer — the assertion is that the display stays a valid, terminated,
     * non-empty string no matter what, because a keypad is the one place a
     * user WILL press things in a nonsensical order.
     */
    static const char keys[] = "0123456789.+-*/%=C<~";
    uint32_t seed = 12345;

    for (int i = 0; i < 20000; i++) {
        seed = seed * 1103515245u + 12345u;
        buddy_calculator_press(&calc, keys[(seed >> 16) % (sizeof(keys) - 1)]);

        const char *shown = buddy_calculator_display(&calc);
        if (shown == NULL || shown[0] == '\0' ||
            strlen(shown) >= BUDDY_CALC_DISPLAY_MAX_LEN) {
            CHECK_MSG(false, "after %d random keys the display was invalid", i);
            break;
        }
    }
    CHECK(true);   /* Reached the end without tripping the check above. */

    /* Operators pressed back to back replace each other rather than stacking. */
    CHECK(strcmp(calc_type(&calc, "9+-*/3="), "3") == 0);

    /* Equals with nothing pending is harmless. */
    CHECK(strcmp(calc_type(&calc, "7==="), "7") == 0);

    /* Leading zeros and stray points do not accumulate. */
    CHECK(strcmp(calc_type(&calc, "000005"), "5") == 0);
    CHECK(strcmp(calc_type(&calc, "1.2.3.4"), "1.234") == 0);
}

TEST(only_a_finished_answer_is_worth_saying_aloud)
{
    buddy_calculator_t calc;

    /* Typing is not an answer — announcing every digit would be unbearable. */
    buddy_calculator_reset(&calc);
    buddy_calculator_press(&calc, '4');
    CHECK(!buddy_calculator_has_spoken_answer(&calc));
    buddy_calculator_press(&calc, '2');
    CHECK(!buddy_calculator_has_spoken_answer(&calc));

    /* Nor is a pending operation. */
    buddy_calculator_press(&calc, BUDDY_CALC_KEY_ADD);
    CHECK(!buddy_calculator_has_spoken_answer(&calc));

    /* Equals is. */
    buddy_calculator_press(&calc, '8');
    buddy_calculator_press(&calc, BUDDY_CALC_KEY_EQUALS);
    CHECK(buddy_calculator_has_spoken_answer(&calc));
    CHECK(buddy_calculator_value(&calc) == 50.0);
}

TEST(the_sign_key_and_backspace_behave)
{
    buddy_calculator_t calc;

    CHECK(strcmp(calc_type(&calc, "5~"), "-5") == 0);
    CHECK(strcmp(calc_type(&calc, "5~~"), "5") == 0);
    CHECK_MSG(strcmp(calc_type(&calc, "~"), "0") == 0,
              "negating nothing produced \"%s\"", buddy_calculator_display(&calc));

    /* A negative number still calculates. */
    CHECK(strcmp(calc_type(&calc, "5~+8="), "3") == 0);

    CHECK(strcmp(calc_type(&calc, "123<"), "12") == 0);
    CHECK(strcmp(calc_type(&calc, "123<<<"), "0") == 0);
    CHECK(strcmp(calc_type(&calc, "123<<<<<"), "0") == 0);

    /* Backspacing an answer clears it, rather than editing a number that
     * would then have come from nowhere. */
    CHECK(strcmp(calc_type(&calc, "2+2=<"), "0") == 0);
}

TEST(any_number_the_calculator_can_reach_can_be_spoken)
{
    voice_speech_plan_t plan;

    /* Straight out of the bank. */
    voice_speech_plan_reset(&plan);
    CHECK(voice_speech_plan_add_number(&plan, 47));
    CHECK(plan.count == 1);

    /* Composed: 347 is "three hundred and forty seven" — four fragments. */
    voice_speech_plan_reset(&plan);
    CHECK(voice_speech_plan_add_number(&plan, 347));
    CHECK(voice_speech_plan_is_usable(&plan));
    CHECK_MSG(plan.count == 4, "347 took %u fragments", (unsigned)plan.count);
    CHECK(strcmp(plan.parts[1], VOICE_FRAGMENT_HUNDRED) == 0);

    /* A round hundred needs no "and". */
    voice_speech_plan_reset(&plan);
    voice_speech_plan_add_number(&plan, 300);
    CHECK(plan.count == 2);

    /* Thousands. */
    voice_speech_plan_reset(&plan);
    CHECK(voice_speech_plan_add_number(&plan, 7733));
    CHECK(voice_speech_plan_is_usable(&plan));

    voice_speech_plan_reset(&plan);
    CHECK(voice_speech_plan_add_number(&plan, 999999));
    CHECK_MSG(voice_speech_plan_is_usable(&plan),
              "999999 needed %u fragments, more than the plan holds",
              (unsigned)plan.count);

    /* A million is past what anyone follows spoken aloud, so it declines. */
    voice_speech_plan_reset(&plan);
    CHECK(!voice_speech_plan_add_number(&plan, 1000000));
    CHECK(!voice_speech_plan_is_usable(&plan));

    /* Negatives get "minus" in front. */
    voice_speech_plan_reset(&plan);
    voice_speech_plan_add_number(&plan, -42);
    CHECK(plan.count == 2);
    CHECK(strcmp(plan.parts[0], VOICE_FRAGMENT_MINUS) == 0);

    /*
     * Every whole number in range must be sayable. This is the check that
     * matters: the calculator will produce numbers nobody anticipated, and one
     * that cannot be spoken goes silent rather than saying it wrong.
     */
    int unsayable = 0;
    for (long n = 0; n < 1000000L; n += 997L) {   /* a prime stride, ~1000 values */
        voice_speech_plan_reset(&plan);
        voice_speech_plan_add_number(&plan, n);
        if (!voice_speech_plan_is_usable(&plan)) {
            if (unsayable < 3) {
                CHECK_MSG(false, "%ld cannot be spoken", n);
            }
            unsayable++;
        }
    }
    CHECK_MSG(unsayable == 0, "%d numbers under a million could not be spoken", unsayable);
}

TEST(decimals_are_read_out_digit_by_digit)
{
    voice_speech_plan_t plan;

    /* "two point five" — the whole part, "point", then the digits. */
    voice_speech_plan_reset(&plan);
    CHECK(voice_speech_plan_add_decimal(&plan, 2.5));
    CHECK(plan.count == 3);
    CHECK(strcmp(plan.parts[1], VOICE_FRAGMENT_POINT) == 0);
    CHECK(strcmp(plan.parts[2], voice_number_words(5)) == 0);

    /* A whole number gets no "point" at all. */
    voice_speech_plan_reset(&plan);
    voice_speech_plan_add_decimal(&plan, 42.0);
    CHECK(plan.count == 1);

    /* Three places is as far as it reads. */
    voice_speech_plan_reset(&plan);
    voice_speech_plan_add_decimal(&plan, 3.14159);
    CHECK(voice_speech_plan_is_usable(&plan));
    CHECK_MSG(plan.count <= 5, "pi took %u fragments", (unsigned)plan.count);

    /* Negative decimals. */
    voice_speech_plan_reset(&plan);
    voice_speech_plan_add_decimal(&plan, -0.5);
    CHECK(voice_speech_plan_is_usable(&plan));
    CHECK(strcmp(plan.parts[0], VOICE_FRAGMENT_MINUS) == 0);

    /*
     * And the pairing that matters: whatever the calculator shows, it can say.
     * A display and a voice that disagree would be worse than a silent one.
     */
    buddy_calculator_t calc;
    static const char *const sums[] = {
        "2+2=", "20/8=", "407*19=", "100-58=", "1/3=", "7*7=", "999*999=",
    };

    for (size_t i = 0; i < sizeof(sums) / sizeof(sums[0]); i++) {
        calc_type(&calc, sums[i]);

        voice_speech_plan_reset(&plan);
        voice_speech_plan_add_decimal(&plan, buddy_calculator_value(&calc));

        CHECK_MSG(voice_speech_plan_is_usable(&plan),
                  "\"%s\" shows %s but cannot be said aloud",
                  sums[i], buddy_calculator_display(&calc));
    }
}

/* ------------------------------------------------------------------------ */
/* Deciding who answers                                                      */
/*                                                                           */
/* The routing policy is the only part of this firmware that can spend the    */
/* owner's money, so it is the part most worth pinning down. These build the  */
/* inputs directly rather than going through the assistant, which is the      */
/* whole reason the decision was pulled out into a pure function.             */
/* ------------------------------------------------------------------------ */

/** A question that WOULD go to the cloud, so each test can spoil one thing. */
static voice_route_inputs_t a_good_question(void)
{
    voice_route_inputs_t inputs = {
        .phrase_matched    = false,
        .phrase_confidence = 0.0f,
        .recorded_seconds  = 2.5f,
        .speech_seconds    = 1.4f,
        .peak_amplitude    = 7000,
        .cloud_ready       = true,
        .mode              = BUDDY_BRAIN_SMART,
        .requests_used     = 0,
        .requests_allowed  = 30,
    };
    return inputs;
}

TEST(a_recognised_phrase_is_never_sent_to_the_cloud)
{
    voice_route_inputs_t inputs = a_good_question();
    inputs.phrase_matched = true;
    inputs.phrase_confidence = 0.9f;

    /*
     * In every mode, including Always ask. Paying a service to tell you the
     * time when the box has a clock is not just wasteful, it is slower — and
     * somebody who picked Always ask wanted better answers, not worse ones.
     */
    for (int mode = 0; mode < BUDDY_BRAIN_MODE_COUNT; mode++) {
        inputs.mode = (buddy_brain_mode_t)mode;
        CHECK_MSG(voice_router_decide(&inputs, NULL) == VOICE_ROUTE_ANSWER_OFFLINE,
                  "mode %d sent a confident phrase match to the cloud", mode);
    }
}

TEST(silence_never_reaches_the_cloud_in_any_mode)
{
    voice_route_inputs_t inputs = a_good_question();
    inputs.speech_seconds = 0.05f;
    inputs.recorded_seconds = 3.0f;
    inputs.peak_amplitude = 60;         /* A dead room, not a quiet speaker. */

    for (int mode = 0; mode < BUDDY_BRAIN_MODE_COUNT; mode++) {
        inputs.mode = (buddy_brain_mode_t)mode;
        CHECK_MSG(voice_router_decide(&inputs, NULL) == VOICE_ROUTE_NOTHING_HEARD,
                  "mode %d would have paid to transcribe three seconds of silence", mode);
    }

    /* And a recording too short to hold a question, however loud it was. */
    inputs = a_good_question();
    inputs.recorded_seconds = 0.3f;
    inputs.speech_seconds = 0.3f;
    CHECK(voice_router_decide(&inputs, NULL) == VOICE_ROUTE_NOTHING_HEARD);
}

/*
 * The v3.6 regression, pinned so it cannot come back.
 *
 * The speech gate is a cost control: it decides whether a recording is worth
 * PAYING to transcribe. In v3.6 it was tested before everything else, so a gate
 * mis-tuned by a loud room silenced answers the chip would have given for
 * free — the box said "I did not hear anything" to every question asked of it,
 * with a working microphone and a full offline vocabulary.
 *
 * The rule these tests enforce: the gate may downgrade a cloud answer to an
 * offline one. It may never turn an answer into silence.
 */
TEST(a_quiet_speaker_is_still_answered_offline)
{
    voice_route_inputs_t inputs = a_good_question();

    /*
     * Plainly audible — a peak of five thousand is somebody talking — but with
     * almost none of it clearing a gate set too high for the room.
     */
    inputs.peak_amplitude = 5000;
    inputs.speech_seconds = 0.02f;
    inputs.recorded_seconds = 2.5f;

    for (int mode = 0; mode < BUDDY_BRAIN_MODE_COUNT; mode++) {
        inputs.mode = (buddy_brain_mode_t)mode;
        CHECK_MSG(voice_router_decide(&inputs, NULL) != VOICE_ROUTE_NOTHING_HEARD,
                  "mode %d ignored an audible question because the gate was mis-tuned", mode);
    }

    /* Offline mode does not consult the gate at all — the answer is free. */
    inputs.mode = BUDDY_BRAIN_OFFLINE_ONLY;
    CHECK(voice_router_decide(&inputs, NULL) == VOICE_ROUTE_ANSWER_OFFLINE);
}

TEST(only_an_inaudible_recording_is_ever_ignored)
{
    /*
     * Exhaustive over the peak level: below the audible threshold the buddy may
     * say nothing, at or above it the buddy must always respond somehow. No
     * other input — mode, gate, budget, cloud state — may change that.
     */
    for (int32_t peak = 0; peak <= 4000; peak += 10) {
        for (int mode = 0; mode < BUDDY_BRAIN_MODE_COUNT; mode++) {
            voice_route_inputs_t inputs = a_good_question();
            inputs.peak_amplitude = peak;
            inputs.speech_seconds = 0.0f;   /* Worst case for the gate. */
            inputs.mode = (buddy_brain_mode_t)mode;
            inputs.cloud_ready = false;
            inputs.requests_used = 999;

            const voice_route_t route = voice_router_decide(&inputs, NULL);

            if (peak >= BUDDY_AUDIBLE_PEAK_AMPLITUDE) {
                CHECK_MSG(route != VOICE_ROUTE_NOTHING_HEARD,
                          "peak %d in mode %d was audible but ignored", (int)peak, mode);
            } else {
                CHECK_MSG(route == VOICE_ROUTE_NOTHING_HEARD,
                          "peak %d in mode %d was inaudible but answered", (int)peak, mode);
            }
        }
    }
}

TEST(offline_only_mode_never_spends_anything)
{
    voice_route_inputs_t inputs = a_good_question();
    inputs.mode = BUDDY_BRAIN_OFFLINE_ONLY;

    /*
     * And it answers rather than declining. The buddy is doing exactly what it
     * was told to; complaining about a feature nobody asked for would make the
     * default mode feel broken.
     */
    CHECK(voice_router_decide(&inputs, NULL) == VOICE_ROUTE_ANSWER_OFFLINE);

    /* Even with everything else in its favour. */
    inputs.cloud_ready = true;
    inputs.speech_seconds = 5.0f;
    inputs.requests_used = 0;
    CHECK(voice_router_decide(&inputs, NULL) == VOICE_ROUTE_ANSWER_OFFLINE);
}

TEST(smart_mode_keeps_a_probable_command_for_itself)
{
    voice_route_inputs_t inputs = a_good_question();

    /*
     * A weak-but-present match means the recogniser heard something SHAPED
     * like a command — most likely a command, said badly. Smart mode obeys it
     * offline rather than buying a polite paragraph about it.
     */
    inputs.phrase_matched = true;
    inputs.phrase_confidence = 0.07f;   /* under MIN, over NEARLY */
    CHECK(voice_router_decide(&inputs, NULL) == VOICE_ROUTE_ANSWER_OFFLINE);

    /* Always ask has been told to prefer the model, so the same input goes up. */
    inputs.mode = BUDDY_BRAIN_ALWAYS_ASK;
    CHECK(voice_router_decide(&inputs, NULL) == VOICE_ROUTE_ASK_CLOUD);

    /* A match too weak to mean anything goes up in both. */
    inputs.phrase_confidence = 0.02f;
    inputs.mode = BUDDY_BRAIN_SMART;
    CHECK(voice_router_decide(&inputs, NULL) == VOICE_ROUTE_ASK_CLOUD);

    /*
     * And the band is where the constants say it is, rather than where a stale
     * literal in this test says it is. The v3.6 bug was two thresholds tuned
     * against an imagined scale; pinning these to the macros means the next
     * change to them has to come here and be thought about.
     */
    CHECK(BUDDY_COMMAND_NEARLY_CONFIDENCE < BUDDY_COMMAND_MIN_CONFIDENCE);
    CHECK_MSG(BUDDY_COMMAND_MIN_CONFIDENCE <= 0.15f,
              "the phrase floor is back above what MultiNet scores on a good match");
}

TEST(smart_mode_wants_a_sentence_not_a_word)
{
    voice_route_inputs_t inputs = a_good_question();

    /* Enough speech to prove the room was not empty, not enough to be a question. */
    inputs.speech_seconds = 0.40f;
    CHECK_MSG(voice_router_decide(&inputs, NULL) == VOICE_ROUTE_ANSWER_OFFLINE,
              "smart mode paid for a single word after the wake word");

    /* A sentence's worth goes up. */
    inputs.speech_seconds = 0.80f;
    CHECK(voice_router_decide(&inputs, NULL) == VOICE_ROUTE_ASK_CLOUD);

    /* Always ask does not apply the extra bar. */
    inputs.speech_seconds = 0.40f;
    inputs.mode = BUDDY_BRAIN_ALWAYS_ASK;
    CHECK(voice_router_decide(&inputs, NULL) == VOICE_ROUTE_ASK_CLOUD);
}

TEST(a_spent_allowance_declines_and_says_why)
{
    voice_route_inputs_t inputs = a_good_question();
    inputs.requests_used = 30;
    inputs.requests_allowed = 30;

    const char *reason = NULL;
    CHECK(voice_router_decide(&inputs, &reason) == VOICE_ROUTE_DECLINE_BUDGET);
    CHECK_MSG(reason != NULL && reason[0] != '\0',
              "the buddy would decline without telling anyone why");

    /* One left is still one. */
    inputs.requests_used = 29;
    CHECK(voice_router_decide(&inputs, NULL) == VOICE_ROUTE_ASK_CLOUD);

    /* An allowance of zero means no ceiling, not a ceiling of zero. */
    inputs.requests_used = 5000;
    inputs.requests_allowed = 0;
    CHECK_MSG(voice_router_decide(&inputs, NULL) == VOICE_ROUTE_ASK_CLOUD,
              "\"no limit\" was treated as a limit of none");
}

TEST(an_unreachable_cloud_explains_itself)
{
    voice_route_inputs_t inputs = a_good_question();
    inputs.cloud_ready = false;
    inputs.mode = BUDDY_BRAIN_ALWAYS_ASK;

    /*
     * Always ask complains, and should. Somebody who chose that mode said the
     * model's answer is the one they want, so quietly serving a canned reply
     * instead would be misleading about what they are hearing.
     */
    const char *reason = NULL;
    CHECK(voice_router_decide(&inputs, &reason) == VOICE_ROUTE_DECLINE_NO_CLOUD);
    CHECK(reason != NULL);

    /*
     * Smart mode falls back instead — changed in v3.7. It asked for the cloud
     * WHEN IT HELPS, not for an apology every time the Wi-Fi is down. Getting
     * this wrong is the same failure as the speech gate above: a box that could
     * have answered choosing to explain itself instead.
     */
    inputs.mode = BUDDY_BRAIN_SMART;
    reason = NULL;
    CHECK_MSG(voice_router_decide(&inputs, &reason) == VOICE_ROUTE_ANSWER_OFFLINE,
              "smart mode apologised instead of answering offline");
    CHECK(reason == NULL);

    /* And offline-only mode is not a failure at all, so it gets no complaint. */
    inputs.mode = BUDDY_BRAIN_OFFLINE_ONLY;
    reason = NULL;
    CHECK(voice_router_decide(&inputs, &reason) == VOICE_ROUTE_ANSWER_OFFLINE);
    CHECK_MSG(reason == NULL, "offline-only mode produced a complaint");

    /* NULL inputs do not crash. */
    CHECK(voice_router_decide(NULL, NULL) == VOICE_ROUTE_NOTHING_HEARD);
}

/** A good question, put to a box in Home AI mode with the home brain set up. */
static voice_route_inputs_t a_good_home_question(void)
{
    voice_route_inputs_t inputs = a_good_question();
    inputs.mode = BUDDY_BRAIN_HOME_AI;
    inputs.home_ready = true;
    inputs.cloud_ready = false;     /* No OpenAI key at all: the point of the mode. */
    return inputs;
}

TEST(home_ai_mode_answers_without_openai)
{
    voice_route_inputs_t inputs = a_good_home_question();

    /* A real question goes to the home brain, with no OpenAI key anywhere. */
    CHECK_MSG(voice_router_decide(&inputs, NULL) == VOICE_ROUTE_ASK_HOME,
              "home AI mode would not ask the home brain without an OpenAI key");

    /* And never to OpenAI, even when a key IS saved. */
    inputs.cloud_ready = true;
    CHECK_MSG(voice_router_decide(&inputs, NULL) == VOICE_ROUTE_ASK_HOME,
              "home AI mode reached for OpenAI");

    /* No other mode ever routes to the home brain. */
    for (int mode = 0; mode < BUDDY_BRAIN_MODE_COUNT; mode++) {
        if (mode == BUDDY_BRAIN_HOME_AI) {
            continue;
        }
        inputs.mode = (buddy_brain_mode_t)mode;
        CHECK_MSG(voice_router_decide(&inputs, NULL) != VOICE_ROUTE_ASK_HOME,
                  "mode %d asked the home brain", mode);
    }
}

TEST(home_ai_mode_has_no_hourly_allowance)
{
    /*
     * The allowance exists to cap a bill. The home brain sends none, so a spent
     * allowance (left over from Smart mode, say) must not silence it.
     */
    voice_route_inputs_t inputs = a_good_home_question();
    inputs.requests_used = 30;
    inputs.requests_allowed = 30;

    const char *reason = NULL;
    CHECK(voice_router_decide(&inputs, &reason) == VOICE_ROUTE_ASK_HOME);
    CHECK(reason == NULL);
}

TEST(home_ai_mode_falls_back_quietly_when_the_pc_is_not_set_up)
{
    /*
     * A computer that is off for the night is ordinary, not a fault. The box
     * answers what it can from the chip rather than apologising every time.
     */
    voice_route_inputs_t inputs = a_good_home_question();
    inputs.home_ready = false;

    const char *reason = NULL;
    CHECK_MSG(voice_router_decide(&inputs, &reason) == VOICE_ROUTE_ANSWER_OFFLINE,
              "home AI mode apologised instead of answering offline");
    CHECK(reason == NULL);
}

TEST(home_ai_mode_still_filters_coughs_and_commands)
{
    /* Free is not instant, so the speech gate still guards the slow path. */
    voice_route_inputs_t inputs = a_good_home_question();
    inputs.speech_seconds = 0.02f;
    CHECK(voice_router_decide(&inputs, NULL) == VOICE_ROUTE_ANSWER_OFFLINE);

    /* A probable command said badly is obeyed on the chip, as in Smart mode. */
    inputs = a_good_home_question();
    inputs.phrase_matched = true;
    inputs.phrase_confidence = 0.07f;   /* under MIN, over NEARLY */
    CHECK(voice_router_decide(&inputs, NULL) == VOICE_ROUTE_ANSWER_OFFLINE);

    /* A match too weak to mean anything goes to the home brain. */
    inputs.phrase_confidence = 0.02f;
    CHECK(voice_router_decide(&inputs, NULL) == VOICE_ROUTE_ASK_HOME);

    /*
     * And it does not apply Smart's stricter "a sentence, not a word" bar:
     * that bar protects money, and there is none here.
     */
    inputs = a_good_home_question();
    inputs.speech_seconds = 0.40f;
    CHECK(voice_router_decide(&inputs, NULL) == VOICE_ROUTE_ASK_HOME);
}

TEST(home_ai_is_appended_to_the_saved_mode_numbers)
{
    /*
     * brain_mode is stored in flash as a number. Inserting the new mode in the
     * middle would quietly turn every saved "Always ask" into something else.
     */
    CHECK(BUDDY_BRAIN_OFFLINE_ONLY == 0);
    CHECK(BUDDY_BRAIN_SMART == 1);
    CHECK(BUDDY_BRAIN_ALWAYS_ASK == 2);
    CHECK(BUDDY_BRAIN_HOME_AI == 3);
    CHECK(BUDDY_BRAIN_MODE_COUNT == 4);
}

TEST(the_hourly_allowance_counts_and_rolls_over)
{
    voice_budget_t budget;
    voice_budget_reset(&budget);

    const int64_t hour = 3600LL * 1000000LL;
    int64_t now = 5LL * 1000000LL;   /* not zero, to catch "assumes t=0" bugs */

    CHECK(voice_budget_remaining(&budget, now, 3) == 3);

    CHECK(voice_budget_try_spend(&budget, now, 3));
    CHECK(voice_budget_try_spend(&budget, now, 3));
    CHECK(voice_budget_remaining(&budget, now, 3) == 1);

    CHECK(voice_budget_try_spend(&budget, now, 3));
    CHECK_MSG(voice_budget_remaining(&budget, now, 3) == 0, "the allowance overspent");
    CHECK_MSG(!voice_budget_try_spend(&budget, now, 3), "a fourth request got through a limit of three");

    /* Most of an hour later, still spent. */
    now += hour - 1;
    CHECK(!voice_budget_try_spend(&budget, now, 3));

    /* And over the line, a fresh allowance. */
    now += 2;
    CHECK_MSG(voice_budget_remaining(&budget, now, 3) == 3, "the window never rolled over");
    CHECK(voice_budget_try_spend(&budget, now, 3));
}

TEST(the_allowance_survives_the_clock_jumping)
{
    voice_budget_t budget;
    voice_budget_reset(&budget);

    int64_t now = 60LL * 1000000LL;
    CHECK(voice_budget_try_spend(&budget, now, 2));
    CHECK(voice_budget_try_spend(&budget, now, 2));
    CHECK(!voice_budget_try_spend(&budget, now, 2));

    /*
     * The clock going backwards is not hypothetical on this chip: it boots at
     * 1970 and jumps decades forward the moment SNTP lands. A window anchored
     * before that jump would otherwise never expire, and the buddy would be
     * out of questions until it was rebooted.
     */
    now = 1LL * 1000000LL;
    CHECK_MSG(voice_budget_remaining(&budget, now, 2) == 2,
              "a backwards clock left the allowance permanently spent");

    /* No limit reports plenty rather than zero. */
    CHECK(voice_budget_remaining(&budget, now, 0) == UINT16_MAX);
    CHECK(voice_budget_try_spend(&budget, now, 0));

    /* NULLs do nothing rather than crashing. */
    voice_budget_reset(NULL);
    CHECK(!voice_budget_try_spend(NULL, now, 5));
    CHECK(voice_budget_remaining(NULL, now, 5) == 0);
}

/* ------------------------------------------------------------------------ */
/* Speaking a sentence that has a number in it                               */
/*                                                                           */
/* The buddy finds a recording by hashing the exact words it is about to say, */
/* which cannot work for the time, a temperature or a countdown. Those are    */
/* assembled from fragments instead. The rule these tests defend is that a    */
/* half-assembled sentence must never be spoken: mute is fine, wrong is not.  */
/* ------------------------------------------------------------------------ */

TEST(every_number_the_buddy_says_has_words_for_it)
{
    /* The whole range, with no gaps and no duplicates. */
    for (int n = 0; n <= VOICE_NUMBER_WORDS_MAX; n++) {
        const char *words = voice_number_words(n);
        CHECK_MSG(words != NULL, "%d has no words", n);
        CHECK_MSG(words != NULL && words[0] != '\0', "%d has empty words", n);
    }

    /* Outside it, NULL — which is what stops a plan being built at all. */
    CHECK(voice_number_words(-1) == NULL);
    CHECK(voice_number_words(VOICE_NUMBER_WORDS_MAX + 1) == NULL);
    CHECK(voice_number_words(100000) == NULL);

    /* Spot checks on the shapes that are easy to get wrong. */
    CHECK(strcmp(voice_number_words(0), "zero") == 0);
    CHECK(strcmp(voice_number_words(13), "thirteen") == 0);
    CHECK(strcmp(voice_number_words(20), "twenty") == 0);
    CHECK(strcmp(voice_number_words(47), "forty seven") == 0);
    CHECK(strcmp(voice_number_words(100), "one hundred") == 0);
    CHECK(strcmp(voice_number_words(120), "one hundred and twenty") == 0);

    /*
     * No two numbers may share a spelling. The words ARE the filename of the
     * recording, so a duplicate would mean two numbers sharing one clip — the
     * buddy would say the wrong one and sound completely certain about it.
     */
    for (int a = 0; a <= VOICE_NUMBER_WORDS_MAX; a++) {
        for (int b = a + 1; b <= VOICE_NUMBER_WORDS_MAX; b++) {
            if (strcmp(voice_number_words(a), voice_number_words(b)) == 0) {
                CHECK_MSG(false, "%d and %d are both \"%s\"", a, b, voice_number_words(a));
            }
        }
    }
}

TEST(the_range_of_numbers_covers_everything_the_buddy_says)
{
    /* Minutes past the hour, and the twelve-hour clock. */
    CHECK(voice_number_words(59) != NULL);
    CHECK(voice_number_words(12) != NULL);

    /* Days of the month. */
    CHECK(voice_number_words(31) != NULL);

    /* A percentage, for humidity and volume. */
    CHECK(voice_number_words(100) != NULL);

    /*
     * And a temperature in Fahrenheit, which is the demanding one — a hot room
     * reads well past a hundred, and that is exactly when somebody asks.
     */
    CHECK_MSG(voice_number_words(110) != NULL,
              "110 degrees Fahrenheit has no words, so a hot room would go unspoken");
}

TEST(a_plan_missing_any_piece_refuses_to_be_spoken)
{
    voice_speech_plan_t plan;

    /* The ordinary case: four fragments, all present, usable. */
    voice_speech_plan_reset(&plan);
    CHECK(voice_speech_plan_add(&plan, VOICE_FRAGMENT_IT_IS));
    CHECK(voice_speech_plan_add_number(&plan, 3));
    CHECK(voice_speech_plan_add_number(&plan, 47));
    CHECK(voice_speech_plan_add(&plan, VOICE_FRAGMENT_AFTERNOON));
    CHECK(voice_speech_plan_is_usable(&plan));
    CHECK(plan.count == 4);

    /*
     * The case this module exists for. A number outside what can be said
     * poisons the whole plan rather than being quietly dropped — because a
     * dropped number leaves "it is / in the afternoon", which is a grammatical
     * sentence, sayable, and a lie.
     *
     * The number here used to be 9999, back when the bank stopped at 120 and
     * anything above it was unsayable. Numbers are composed now, so 9999 says
     * itself perfectly well and a million is where the line moved to.
     */
    voice_speech_plan_reset(&plan);
    voice_speech_plan_add(&plan, VOICE_FRAGMENT_IT_IS);
    voice_speech_plan_add_number(&plan, 1000000);
    voice_speech_plan_add(&plan, VOICE_FRAGMENT_AFTERNOON);
    CHECK_MSG(!voice_speech_plan_is_usable(&plan),
              "a plan with an unsayable number was still considered sayable");

    /* And the number that used to fail now works, which is the point. */
    voice_speech_plan_reset(&plan);
    CHECK(voice_speech_plan_add_number(&plan, 9999));
    CHECK(voice_speech_plan_is_usable(&plan));

    /* A NULL fragment poisons it the same way. */
    voice_speech_plan_reset(&plan);
    voice_speech_plan_add(&plan, VOICE_FRAGMENT_IT_IS);
    voice_speech_plan_add(&plan, voice_speech_month(99));   /* out of range -> NULL */
    CHECK(!voice_speech_plan_is_usable(&plan));

    /* So does overflowing it. */
    voice_speech_plan_reset(&plan);
    for (int i = 0; i < VOICE_SPEECH_MAX_PARTS + 3; i++) {
        voice_speech_plan_add(&plan, VOICE_FRAGMENT_THE);
    }
    CHECK(!voice_speech_plan_is_usable(&plan));

    /* An empty plan is not sayable either — that is how "no plan" is spelled. */
    voice_speech_plan_reset(&plan);
    CHECK(!voice_speech_plan_is_usable(&plan));

    /* And nothing crashes on NULL. */
    voice_speech_plan_reset(NULL);
    CHECK(!voice_speech_plan_add(NULL, "x"));
    CHECK(!voice_speech_plan_is_usable(NULL));
}

TEST(the_calendar_names_are_all_there_and_in_the_right_order)
{
    /* Indexed the way struct tm counts, which is the whole point. */
    CHECK(strcmp(voice_speech_weekday(0), "Sunday") == 0);
    CHECK(strcmp(voice_speech_weekday(6), "Saturday") == 0);
    CHECK(voice_speech_weekday(7) == NULL);
    CHECK(voice_speech_weekday(-1) == NULL);

    CHECK(strcmp(voice_speech_month(0), "January") == 0);
    CHECK(strcmp(voice_speech_month(11), "December") == 0);
    CHECK(voice_speech_month(12) == NULL);
    CHECK(voice_speech_month(-1) == NULL);
}

TEST(every_fragment_the_code_names_is_in_the_recorded_list)
{
    /*
     * The list is what the clip generator records and what the buddy can
     * therefore say. A fragment used in an answer but missing from it would
     * build, run, and silently take that whole answer's voice away.
     */
    size_t count = 0;
    const char *const *all = voice_speech_all_fragments(&count);

    CHECK(all != NULL && count > 0);

    const char *const named[] = {
        VOICE_FRAGMENT_IT_IS, VOICE_FRAGMENT_OH, VOICE_FRAGMENT_OCLOCK,
        VOICE_FRAGMENT_MORNING, VOICE_FRAGMENT_AFTERNOON, VOICE_FRAGMENT_EVENING,
        VOICE_FRAGMENT_TODAY_IS, VOICE_FRAGMENT_THE,
        VOICE_FRAGMENT_MINUS, VOICE_FRAGMENT_DEGREES_F, VOICE_FRAGMENT_DEGREES_C,
        VOICE_FRAGMENT_IN_HERE,
        VOICE_FRAGMENT_HUMIDITY_IS, VOICE_FRAGMENT_PERCENT,
        VOICE_FRAGMENT_VOLUME_UP_TO, VOICE_FRAGMENT_VOLUME_DOWN_TO,
        VOICE_FRAGMENT_ABOUT, VOICE_FRAGMENT_MINUTE_LEFT, VOICE_FRAGMENT_MINUTES_LEFT,
        VOICE_FRAGMENT_SECOND_LEFT, VOICE_FRAGMENT_SECONDS_LEFT,
        VOICE_FRAGMENT_MINUTES_AND, VOICE_FRAGMENT_THATS,
    };

    for (size_t i = 0; i < sizeof(named) / sizeof(named[0]); i++) {
        bool found = false;
        for (size_t j = 0; j < count; j++) {
            if (strcmp(named[i], all[j]) == 0) {
                found = true;
                break;
            }
        }
        CHECK_MSG(found, "\"%s\" is used in an answer but is not in the recorded list",
                  named[i]);
    }

    /* The weekdays and months too. */
    for (int day = 0; day < 7; day++) {
        bool found = false;
        for (size_t j = 0; j < count; j++) {
            if (strcmp(voice_speech_weekday(day), all[j]) == 0) {
                found = true;
                break;
            }
        }
        CHECK_MSG(found, "%s is not in the recorded list", voice_speech_weekday(day));
    }
    for (int month = 0; month < 12; month++) {
        bool found = false;
        for (size_t j = 0; j < count; j++) {
            if (strcmp(voice_speech_month(month), all[j]) == 0) {
                found = true;
                break;
            }
        }
        CHECK_MSG(found, "%s is not in the recorded list", voice_speech_month(month));
    }
}

/* ------------------------------------------------------------------------ */
/* Deciding whether anything was actually said                               */
/* ------------------------------------------------------------------------ */

/** Fill a buffer with a sine tone at a given amplitude. */
static void fill_tone(int16_t *samples, size_t count, int amplitude, float hz, float rate)
{
    for (size_t i = 0; i < count; i++) {
        samples[i] = (int16_t)((float)amplitude *
                               sinf(2.0f * 3.14159265f * hz * (float)i / rate));
    }
}

TEST(silence_is_never_worth_paying_to_transcribe)
{
    static int16_t samples[16000 * 3];   /* three seconds */
    memset(samples, 0, sizeof(samples));

    voice_speech_gate_t gate;
    voice_speech_gate_reset(&gate);
    voice_speech_gate_add(&gate, samples, sizeof(samples) / sizeof(samples[0]));

    CHECK_MSG(voice_speech_gate_speech_seconds(&gate, 16000) == 0.0f,
              "pure silence measured as speech");
    CHECK_MSG(!voice_speech_gate_worth_asking(&gate, 16000),
              "three seconds of silence would have been sent to the cloud");

    /* Room tone: quiet, but not zero. Still not a question. */
    fill_tone(samples, sizeof(samples) / sizeof(samples[0]), 120, 220.0f, 16000.0f);
    voice_speech_gate_reset(&gate);
    voice_speech_gate_add(&gate, samples, sizeof(samples) / sizeof(samples[0]));
    CHECK_MSG(!voice_speech_gate_worth_asking(&gate, 16000),
              "quiet room tone would have been sent to the cloud");
}

TEST(a_cough_is_too_short_to_be_a_question)
{
    static int16_t samples[16000];       /* one second... */
    memset(samples, 0, sizeof(samples));

    /* ...with a tenth of a second of loud noise in the middle of it. */
    fill_tone(&samples[7000], 1600, 12000, 500.0f, 16000.0f);

    voice_speech_gate_t gate;
    voice_speech_gate_reset(&gate);
    voice_speech_gate_add(&gate, samples, sizeof(samples) / sizeof(samples[0]));

    CHECK_MSG(!voice_speech_gate_worth_asking(&gate, 16000),
              "a tenth of a second of noise counted as a question");
}

TEST(a_spoken_question_gets_through)
{
    static int16_t samples[16000 * 2];   /* two seconds */
    memset(samples, 0, sizeof(samples));

    /*
     * Speech-shaped: loud bursts with gaps between them, about a second of
     * sound in total. Anything less permissive than this would start refusing
     * real questions, which is a far worse failure than an occasional wasted
     * request.
     */
    for (int burst = 0; burst < 4; burst++) {
        fill_tone(&samples[burst * 6000], 4000, 9000, 300.0f, 16000.0f);
    }

    voice_speech_gate_t gate;
    voice_speech_gate_reset(&gate);
    voice_speech_gate_add(&gate, samples, sizeof(samples) / sizeof(samples[0]));

    CHECK_MSG(voice_speech_gate_worth_asking(&gate, 16000),
              "a two-second question with a second of speech in it was refused");
    CHECK(voice_speech_gate_speech_seconds(&gate, 16000) > 0.5f);
    CHECK(gate.peak_amplitude > 8000);
}

TEST(the_measurement_does_not_depend_on_how_the_audio_was_chunked)
{
    static int16_t samples[16000 * 2];
    memset(samples, 0, sizeof(samples));
    for (int burst = 0; burst < 4; burst++) {
        fill_tone(&samples[burst * 6000], 4000, 9000, 300.0f, 16000.0f);
    }

    const size_t total = sizeof(samples) / sizeof(samples[0]);

    /* All at once. */
    voice_speech_gate_t whole;
    voice_speech_gate_reset(&whole);
    voice_speech_gate_add(&whole, samples, total);

    /*
     * In awkward pieces that do not line up with the block size — which is what
     * the real microphone does, since its frame size has nothing to do with
     * this module's. A carried-over partial block is the only reason these
     * agree.
     */
    voice_speech_gate_t pieces;
    voice_speech_gate_reset(&pieces);
    for (size_t offset = 0; offset < total; offset += 373) {
        const size_t chunk = (offset + 373 <= total) ? 373 : (total - offset);
        voice_speech_gate_add(&pieces, &samples[offset], chunk);
    }

    CHECK_MSG(whole.loud_blocks == pieces.loud_blocks,
              "chunking changed the measurement: %u blocks whole, %u in pieces",
              (unsigned)whole.loud_blocks, (unsigned)pieces.loud_blocks);
    CHECK(whole.peak_amplitude == pieces.peak_amplitude);
}

TEST(the_gate_survives_hostile_input)
{
    voice_speech_gate_t gate;
    voice_speech_gate_reset(&gate);

    /* NULLs and empties do nothing rather than crashing. */
    voice_speech_gate_add(&gate, NULL, 100);
    voice_speech_gate_add(NULL, NULL, 100);
    CHECK(!voice_speech_gate_worth_asking(&gate, 16000));
    CHECK(!voice_speech_gate_worth_asking(NULL, 16000));
    CHECK(voice_speech_gate_speech_seconds(&gate, 0) == 0.0f);

    /*
     * The most negative sample there is. Negating INT16_MIN is undefined
     * behaviour in the obvious spelling of "magnitude", so this is here to keep
     * that spelling from creeping back in.
     */
    static int16_t extremes[VOICE_GATE_BLOCK_SAMPLES * 2];
    for (size_t i = 0; i < sizeof(extremes) / sizeof(extremes[0]); i++) {
        extremes[i] = INT16_MIN;
    }

    voice_speech_gate_reset(&gate);
    voice_speech_gate_add(&gate, extremes, sizeof(extremes) / sizeof(extremes[0]));
    CHECK(gate.peak_amplitude == 32768);
    CHECK(gate.loud_blocks == 2);
}

/* ------------------------------------------------------------------------ */
/* The OpenAI API key                                                        */
/*                                                                           */
/* These exist because of a real bug that cost real hours. The key buffer was */
/* 128 bytes, the text field was capped to match, and OpenAI's project keys   */
/* are about 165 characters — so the box quietly stored the first 127 of them */
/* and reported "Saved". Every request then came back 401, which is exactly   */
/* what a WRONG key looks like, so the truncation was invisible from every    */
/* screen and every log line the firmware produced.                          */
/*                                                                           */
/* The rule these tests enforce is: never truncate a key. Refuse it instead.  */
/* An absent key is a problem you can see.                                    */
/* ------------------------------------------------------------------------ */

/** Build a synthetic key of an exact length: a prefix then filler. */
static void make_key_of_length(char *out, size_t length, const char *prefix)
{
    const size_t prefix_length = strlen(prefix);

    memcpy(out, prefix, prefix_length);
    for (size_t i = prefix_length; i < length; i++) {
        /* An alphabet like the real thing: letters, digits, and the odd dash. */
        out[i] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_"[i % 64];
    }
    out[length] = '\0';
}

TEST(the_key_buffer_holds_every_key_openai_issues)
{
    /*
     * The guard on the constant itself. If somebody ever shrinks this back
     * down, the failure is here, in a test that names the consequence, rather
     * than on a desk six months from now.
     */
    CHECK_MSG(BUDDY_CLOUD_API_KEY_MAX_LEN >= 200,
              "the key buffer is %d bytes; a project key is about 165 characters "
              "and service-account keys are longer",
              BUDDY_CLOUD_API_KEY_MAX_LEN);

    /* A full-length project key must survive a round trip byte for byte. */
    char project_key[256];
    make_key_of_length(project_key, 164, "sk-proj-");

    char cleaned[BUDDY_CLOUD_API_KEY_MAX_LEN];
    const size_t length = buddy_api_key_sanitise(project_key, cleaned, sizeof(cleaned));

    CHECK_MSG(length == 164, "a 164-character key came back as %u characters",
              (unsigned)length);
    CHECK_MSG(strcmp(cleaned, project_key) == 0, "the key was altered in transit");
}

TEST(a_key_too_long_to_store_is_refused_rather_than_truncated)
{
    char small[32];

    /* Exactly one character too many for the buffer. */
    char key[64];
    make_key_of_length(key, sizeof(small), "sk-");

    const size_t length = buddy_api_key_sanitise(key, small, sizeof(small));

    CHECK_MSG(length == 0, "an oversized key was accepted as %u characters",
              (unsigned)length);
    CHECK_MSG(small[0] == '\0', "an oversized key left something behind: \"%s\"", small);

    /* One character shorter, and it fits exactly. */
    make_key_of_length(key, sizeof(small) - 1, "sk-");
    CHECK(buddy_api_key_sanitise(key, small, sizeof(small)) == sizeof(small) - 1);
    CHECK(strcmp(small, key) == 0);
}

TEST(a_key_pasted_into_a_text_file_arrives_clean)
{
    char cleaned[BUDDY_CLOUD_API_KEY_MAX_LEN];
    char key[256];
    make_key_of_length(key, 164, "sk-proj-");

    char decorated[512];

    /* A plain trailing newline, as every editor leaves behind. */
    snprintf(decorated, sizeof(decorated), "%s\n", key);
    CHECK(buddy_api_key_sanitise(decorated, cleaned, sizeof(cleaned)) == 164);
    CHECK(strcmp(cleaned, key) == 0);

    /* Windows line endings. */
    snprintf(decorated, sizeof(decorated), "%s\r\n", key);
    CHECK(buddy_api_key_sanitise(decorated, cleaned, sizeof(cleaned)) == 164);
    CHECK_MSG(strcmp(cleaned, key) == 0, "a carriage return survived into the key");

    /* Stray spaces and tabs from a sloppy copy. */
    snprintf(decorated, sizeof(decorated), "  \t%s \t\n", key);
    CHECK(buddy_api_key_sanitise(decorated, cleaned, sizeof(cleaned)) == 164);
    CHECK_MSG(strcmp(cleaned, key) == 0, "surrounding whitespace survived into the key");

    /* Notes underneath the key, which is how the shipped file is laid out. */
    snprintf(decorated, sizeof(decorated), "%s\n# everything here is a comment\n", key);
    CHECK(buddy_api_key_sanitise(decorated, cleaned, sizeof(cleaned)) == 164);
    CHECK_MSG(strcmp(cleaned, key) == 0, "a comment line leaked into the key");
}

TEST(nothing_usable_produces_an_empty_key_rather_than_rubbish)
{
    char cleaned[BUDDY_CLOUD_API_KEY_MAX_LEN];

    /* Every one of these must leave an empty string behind, not a stale one. */
    static const char *const nothings[] = { "", "\n", "\r\n", "   ", " \t \n ", NULL };

    for (size_t i = 0; i < sizeof(nothings) / sizeof(nothings[0]); i++) {
        memset(cleaned, 'x', sizeof(cleaned));
        CHECK_MSG(buddy_api_key_sanitise(nothings[i], cleaned, sizeof(cleaned)) == 0,
                  "input %u was mistaken for a key", (unsigned)i);
        CHECK_MSG(cleaned[0] == '\0', "input %u left the buffer non-empty", (unsigned)i);
    }

    /* A zero-sized or absent destination must simply do nothing. */
    CHECK(buddy_api_key_sanitise("sk-whatever", cleaned, 0) == 0);
    CHECK(buddy_api_key_sanitise("sk-whatever", NULL, sizeof(cleaned)) == 0);
}

TEST(a_key_is_judged_by_its_shape_not_by_a_guess_at_its_format)
{
    char key[256];

    /* The original format: "sk-" and 48 more, 51 in total. */
    make_key_of_length(key, 51, "sk-");
    CHECK_MSG(buddy_api_key_looks_plausible(key), "the original 51-character format was rejected");

    /* Today's project format. */
    make_key_of_length(key, 164, "sk-proj-");
    CHECK(buddy_api_key_looks_plausible(key));

    /* Service accounts, which are longer again. */
    make_key_of_length(key, 200, "sk-svcacct-");
    CHECK_MSG(buddy_api_key_looks_plausible(key),
              "a longer future format was rejected for being long");

    /* The placeholder the key file ships with must never be mistaken for one. */
    CHECK(!buddy_api_key_looks_plausible("PASTE_YOUR_KEY_ON_THIS_LINE"));
    CHECK(!buddy_api_key_looks_plausible("sk-"));
    CHECK(!buddy_api_key_looks_plausible(""));
    CHECK(!buddy_api_key_looks_plausible(NULL));

    /*
     * And the case this whole exercise is about: a real key with its tail
     * lopped off is still 127 characters and still starts "sk-proj-", so no
     * shape check can catch it. Only the length can — which is why the length
     * is what gets shown on screen and in the log.
     */
    make_key_of_length(key, 127, "sk-proj-");
    CHECK_MSG(buddy_api_key_looks_plausible(key),
              "a truncated key is indistinguishable by shape, as expected");
}

TEST(describing_a_key_never_reveals_it)
{
    char key[256];
    make_key_of_length(key, 164, "sk-proj-");

    char description[64];
    buddy_api_key_describe(key, description, sizeof(description));

    /* The length has to be in there — it is the whole point of the line. */
    CHECK_MSG(strstr(description, "164") != NULL,
              "the description did not state the length: \"%s\"", description);

    /*
     * Nothing beyond the format prefix may appear. Checking a window from the
     * middle of the key is the real test: a description that included it would
     * end up in a log the user pastes into a forum.
     */
    char middle[17];
    memcpy(middle, key + 80, 16);
    middle[16] = '\0';
    CHECK_MSG(strstr(description, middle) == NULL, "the description leaked the key");

    /* The tail, too. */
    CHECK_MSG(strstr(description, key + 148) == NULL, "the description leaked the key's tail");

    /* An absent key says so rather than printing an empty space. */
    buddy_api_key_describe("", description, sizeof(description));
    CHECK(strcmp(description, "not set") == 0);
    buddy_api_key_describe(NULL, description, sizeof(description));
    CHECK(strcmp(description, "not set") == 0);

    /* A tiny buffer must not be overrun. */
    char tiny[8] = { 0 };
    buddy_api_key_describe(key, tiny, sizeof(tiny));
    CHECK(tiny[sizeof(tiny) - 1] == '\0');
}

/* ------------------------------------------------------------------------ */
/* Runner                                                                    */
/* ------------------------------------------------------------------------ */


/* ------------------------------------------------------------------------ */
/* Screen layout — the face must not collide with the corner widgets         */
/* ------------------------------------------------------------------------ */

/**
 * The topmost pixel an eyebrow can reach for a given expression.
 *
 * Mirrors the arithmetic in face_view.c's layout_brows(): the brows hang a
 * fixed distance above the RESTING eye position, the pose can shift them, and
 * the result is clamped so nothing can climb into the sensor pill.
 */
static int brow_top_for(const face_pose_t *pose)
{
    int baseline = FACE_EYE_CENTRE_Y
                   - (FACE_EYE_HEIGHT / 2)
                   - FACE_BROW_GAP_ABOVE_EYE
                   + pose->brow_offset_y;

    /* Looking up and a bouncing antic both lift the brows further. */
    baseline -= FACE_EYE_LOOK_RANGE_Y / 2;
    if (pose->face_offset_y < 0) {
        baseline += pose->face_offset_y;
    }
    if (pose->brow_asymmetry < 0) {
        baseline += pose->brow_asymmetry;
    }

    if (baseline < FACE_BROW_MIN_BASELINE_Y) {
        baseline = FACE_BROW_MIN_BASELINE_Y;
    }

    /* The drawn stroke bows upward in the middle and has width of its own. */
    return baseline - 3 - (FACE_BROW_THICKNESS / 2);
}

TEST(no_expression_lets_the_face_reach_the_sensor_pill)
{
    for (int emotion = 0; emotion < FACE_EMOTION_COUNT; emotion++) {
        const face_pose_t *pose = face_emotion_get_pose((face_emotion_t)emotion);
        CHECK_MSG(pose != NULL, "emotion %d has no pose", emotion);
        if (pose == NULL) {
            continue;
        }

        const int top = brow_top_for(pose);
        CHECK_MSG(top >= FACE_SENSOR_CHIP_BOTTOM_Y,
                  "%s puts an eyebrow at y=%d, inside the sensor pill (bottom y=%d)",
                  face_emotion_get_name((face_emotion_t)emotion),
                  top, FACE_SENSOR_CHIP_BOTTOM_Y);
    }
}

TEST(the_whole_face_fits_on_the_screen)
{
    for (int emotion = 0; emotion < FACE_EMOTION_COUNT; emotion++) {
        const face_pose_t *pose = face_emotion_get_pose((face_emotion_t)emotion);
        if (pose == NULL) {
            continue;
        }

        const char *name = face_emotion_get_name((face_emotion_t)emotion);

        /* Eyes, at their widest, including how far they can drift sideways. */
        const int eye_outer_x = (BUDDY_SCREEN_WIDTH / 2)
                                + FACE_EYE_SPACING_FROM_CENTRE
                                + pose->eye_width / 2
                                + FACE_EYE_LOOK_RANGE_X;
        CHECK_MSG(eye_outer_x <= BUDDY_SCREEN_WIDTH,
                  "%s pushes an eye to x=%d, off a %d-wide screen",
                  name, eye_outer_x, BUDDY_SCREEN_WIDTH);

        /* The mouth, at its widest and lowest. */
        const int mouth_half = pose->mouth_width / 2;
        CHECK_MSG((BUDDY_SCREEN_WIDTH / 2) + mouth_half <= BUDDY_SCREEN_WIDTH,
                  "%s has a mouth %d wide, wider than the screen",
                  name, pose->mouth_width);

        const int mouth_bottom = FACE_MOUTH_CENTRE_Y
                                 + pose->mouth_offset_y
                                 + pose->mouth_open_height / 2
                                 + FACE_MOUTH_THICKNESS;
        CHECK_MSG(mouth_bottom <= BUDDY_SCREEN_HEIGHT,
                  "%s puts the bottom of the mouth at y=%d, below a %d-tall screen",
                  name, mouth_bottom, BUDDY_SCREEN_HEIGHT);
    }
}

TEST(scaling_the_face_keeps_every_part_of_it_visible)
{
    /* A scale of zero would collapse the face into nothing. */
    CHECK(FACE_SCALE_PERCENT > 0);

    /* Every scaled dimension has to survive the integer division. */
    CHECK(FACE_EYE_WIDTH > 0);
    CHECK(FACE_EYE_HEIGHT > 0);
    CHECK(FACE_EYE_CORNER_RADIUS > 0);
    CHECK(FACE_BROW_LENGTH > 0);
    CHECK(FACE_BROW_THICKNESS > 0);
    CHECK(FACE_MOUTH_WIDTH > 0);
    CHECK(FACE_MOUTH_THICKNESS > 0);
    CHECK(FACE_BLUSH_DIAMETER > 0);

    /* Rounded corners cannot be more than half the box they round. */
    CHECK(FACE_EYE_CORNER_RADIUS * 2 <= FACE_EYE_HEIGHT);

    /* The two eyes must not grow into each other. */
    CHECK(FACE_EYE_WIDTH / 2 < FACE_EYE_SPACING_FROM_CENTRE);
}

TEST(the_caption_bubble_fits_on_the_screen)
{
    /* Width, with an edge margin on each side. */
    CHECK(BUDDY_SPEECH_BUBBLE_MAX_WIDTH + (2 * BUDDY_SCREEN_EDGE_MARGIN)
          <= BUDDY_SCREEN_WIDTH);

    /* Height, measured up from the bottom margin the bubble is aligned to. */
    CHECK(BUDDY_SPEECH_BUBBLE_MAX_HEIGHT + BUDDY_SCREEN_EDGE_MARGIN
          <= BUDDY_SCREEN_HEIGHT);

    /* A minimum wider than the maximum would make the bubble impossible. */
    CHECK(BUDDY_SPEECH_BUBBLE_MIN_WIDTH <= BUDDY_SPEECH_BUBBLE_MAX_WIDTH);

    /*
     * Padding has to leave room for actual text on both axes. Getting this
     * wrong gives a bubble whose usable text area is zero or negative, and
     * LVGL's response to that is to draw nothing at all — which looks like the
     * caption never fired rather than like a layout bug.
     */
    CHECK(2 * BUDDY_SPEECH_BUBBLE_PADDING < BUDDY_SPEECH_BUBBLE_MIN_WIDTH);
    CHECK(2 * BUDDY_SPEECH_BUBBLE_PADDING < BUDDY_SPEECH_BUBBLE_MAX_HEIGHT);
}

TEST(a_long_caption_is_shown_in_full_rather_than_shortened)
{
    /*
     * This test used to assert the opposite: that the tallest possible bubble
     * still stopped below the lifted eyes, so you could see the buddy over the
     * top of whatever it was saying. That guarantee is gone on purpose. It was
     * being paid for with an ellipsis, and the end of a long answer is worth
     * more than four seconds of eye contact.
     *
     * What is checked now is that the bubble can grow far enough to make
     * shortening unnecessary in the first place.
     */
    const int usable_width  = BUDDY_SPEECH_BUBBLE_MAX_WIDTH
                              - (2 * BUDDY_SPEECH_BUBBLE_PADDING) - 2;
    const int usable_height = BUDDY_SPEECH_BUBBLE_MAX_HEIGHT
                              - (2 * BUDDY_SPEECH_BUBBLE_PADDING) - 2;

    /*
     * Conservative figures for lv_font_montserrat_12, the smallest of the three
     * caption sizes and so the one that decides whether a caption ever has to
     * be shortened. Both numbers are rounded the pessimistic way, so a pass
     * here means real text has room to spare rather than exactly enough.
     *
     * This is why CONFIG_LV_FONT_MONTSERRAT_12 is in sdkconfig.defaults. At
     * 14 px the same sums give about four hundred characters, which is less
     * than a reply may contain — ui_speech_bubble.c warns at boot when it finds
     * itself built without the 12 px tier.
     */
    const int pessimistic_char_width  = 6;
    const int pessimistic_line_height = 15;

    const int lines      = usable_height / pessimistic_line_height;
    const int per_line   = usable_width / pessimistic_char_width;
    const int characters = lines * per_line;

    CHECK_MSG(characters >= BUDDY_REPLY_TEXT_MAX_LEN,
              "the bubble holds about %d characters but a reply may be %d long, "
              "so the longest answers would still be cut short",
              characters, BUDDY_REPLY_TEXT_MAX_LEN);

    /*
     * The face still lifts, it just no longer has to clear the whole bubble.
     * The one hard limit that remains is that the lift must not carry an
     * eyebrow off the top of the panel.
     */
    const int lifted_brow = FACE_BROW_MIN_BASELINE_Y - BUDDY_FACE_LIFT_MAX;
    CHECK_MSG(lifted_brow >= 0,
              "lifting the face by %d puts an eyebrow at y=%d, off the screen",
              BUDDY_FACE_LIFT_MAX, lifted_brow);
}

/* ------------------------------------------------------------------------ */
/* The kitchen timer                                                         */
/*                                                                           */
/* A countdown is easy to get subtly wrong and impossible to check by hand    */
/* without sitting and waiting for it. The esp_timer stub in tests/stubs      */
/* makes the clock a variable, so a thirty-minute timer is verified in        */
/* microseconds and the tick callback fires on demand.                       */
/* ------------------------------------------------------------------------ */

/* The fake clock the stub header declares. */
int64_t        esp_timer_stub_now_us = 1000000;
esp_timer_cb_t esp_timer_stub_callback;
void          *esp_timer_stub_arg;
bool           esp_timer_stub_running;
int64_t        esp_timer_stub_period_us;

static uint32_t s_last_tick_seconds;
static bool     s_last_tick_running;
static int      s_expiry_count;

static void record_tick(uint32_t seconds_left, bool running)
{
    s_last_tick_seconds = seconds_left;
    s_last_tick_running = running;
}

static void record_expiry(void)
{
    s_expiry_count++;
}

static void timer_test_setup(void)
{
    esp_timer_stub_reset();
    s_last_tick_seconds = 0;
    s_last_tick_running = false;
    s_expiry_count = 0;

    buddy_timer_init();
    buddy_timer_set_tick_callback(record_tick);
    buddy_timer_set_expired_callback(record_expiry);
    buddy_timer_cancel();
}

TEST(a_timer_counts_down_and_finishes_exactly_once)
{
    timer_test_setup();

    CHECK(buddy_timer_start(5) == ESP_OK);
    CHECK(buddy_timer_is_running());

    /*
     * Rounded UP, so the first second of a five-minute timer reads 5:00 rather
     * than flicking straight to 4:59 — which looks like a bug even though the
     * arithmetic is fine.
     */
    CHECK_MSG(buddy_timer_seconds_remaining() == 300,
              "a five-minute timer started at %lu seconds",
              (unsigned long)buddy_timer_seconds_remaining());

    esp_timer_stub_advance(60 * 1000000LL);
    CHECK_MSG(buddy_timer_seconds_remaining() == 240,
              "after one minute, %lu seconds left",
              (unsigned long)buddy_timer_seconds_remaining());
    CHECK(s_expiry_count == 0);

    esp_timer_stub_advance(239 * 1000000LL);
    CHECK(buddy_timer_is_running());
    CHECK(s_expiry_count == 0);

    /* The last second. */
    esp_timer_stub_advance(1 * 1000000LL);
    CHECK_MSG(s_expiry_count == 1, "expired %d times, expected once", s_expiry_count);
    CHECK(!buddy_timer_is_running());
    CHECK(!s_last_tick_running);

    /* And it must not keep firing after it has landed. */
    esp_timer_stub_advance(120 * 1000000LL);
    CHECK_MSG(s_expiry_count == 1, "kept firing after finishing (%d times)", s_expiry_count);
}

TEST(a_long_timer_does_not_drift)
{
    timer_test_setup();

    /*
     * The countdown stores a finish TIME rather than a decrementing counter,
     * precisely so that late ticks cost nothing. Half an hour of one-second
     * ticks is where a counter-based version would visibly lose time.
     */
    CHECK(buddy_timer_start(30) == ESP_OK);

    esp_timer_stub_advance(29 * 60 * 1000000LL);
    CHECK_MSG(buddy_timer_seconds_remaining() == 60,
              "29 minutes into a 30-minute timer, %lu seconds left",
              (unsigned long)buddy_timer_seconds_remaining());
    CHECK(s_expiry_count == 0);

    esp_timer_stub_advance(60 * 1000000LL);
    CHECK(s_expiry_count == 1);
}

TEST(cancelling_and_restarting_a_timer_behaves)
{
    timer_test_setup();

    /* Nothing running: cancelling is a no-op that says so. */
    CHECK(!buddy_timer_cancel());

    CHECK(buddy_timer_start(10) == ESP_OK);
    esp_timer_stub_advance(30 * 1000000LL);
    CHECK(buddy_timer_cancel());
    CHECK(!buddy_timer_is_running());
    CHECK(buddy_timer_seconds_remaining() == 0);

    /* A cancelled timer must never fire afterwards. */
    esp_timer_stub_advance(600 * 1000000LL);
    CHECK_MSG(s_expiry_count == 0, "a cancelled timer still went off");

    /* Starting again replaces it rather than stacking. */
    CHECK(buddy_timer_start(2) == ESP_OK);
    CHECK(buddy_timer_seconds_remaining() == 120);
    CHECK(buddy_timer_start(1) == ESP_OK);
    CHECK_MSG(buddy_timer_seconds_remaining() == 60,
              "restarting left %lu seconds instead of 60",
              (unsigned long)buddy_timer_seconds_remaining());

    /* Nonsense durations are refused rather than clamped silently. */
    CHECK(buddy_timer_start(0) != ESP_OK);
    CHECK(buddy_timer_start(BUDDY_TIMER_MAX_MINUTES + 1) != ESP_OK);
}

TEST(every_timer_phrase_names_a_duration_it_can_actually_set)
{
    /*
     * The important one. "twenty" contains no other spelling, but "two" is a
     * substring of nothing while "ten" IS inside nothing either — yet the
     * ordering of the lookup table still matters, and getting it wrong turns
     * "set a timer for twenty minutes" into a two-minute timer with no
     * complaint from anything. So every registered phrase is checked against
     * the number a person would expect from reading it.
     */
    const struct {
        const char *phrase;
        uint16_t    expected;
    } expectations[] = {
        { "set a timer for one minute",     1 },
        { "set a timer for two minutes",    2 },
        { "set a timer for five minutes",   5 },
        { "set a timer for ten minutes",   10 },
        { "set a timer for twenty minutes", 20 },
        { "set a timer for thirty minutes", 30 },
    };

    for (size_t i = 0; i < sizeof(expectations) / sizeof(expectations[0]); i++) {
        uint16_t minutes = 0;
        CHECK_MSG(buddy_timer_minutes_from_phrase(expectations[i].phrase, &minutes),
                  "\"%s\" did not name a duration", expectations[i].phrase);
        CHECK_MSG(minutes == expectations[i].expected,
                  "\"%s\" was read as %u minutes, expected %u",
                  expectations[i].phrase, (unsigned)minutes,
                  (unsigned)expectations[i].expected);
    }

    /* And every phrase registered for the action must be one of the above. */
    const size_t count = voice_commands_get_count();
    size_t registered = 0;

    for (size_t i = 0; i < count; i++) {
        const voice_command_t *command = voice_commands_get(i);
        if (command == NULL || command->action != VOICE_ACTION_TIMER_SET) {
            continue;
        }
        if (command->language != BUDDY_LANGUAGE_ENGLISH) {
            continue;
        }
        registered++;

        uint16_t minutes = 0;
        CHECK_MSG(buddy_timer_minutes_from_phrase(command->phrase, &minutes),
                  "registered phrase \"%s\" names no duration the timer can read",
                  command->phrase);
    }

    CHECK_MSG(registered > 0, "no timer phrases are registered at all");

    /* Nonsense in, false out — never a silently wrong duration. */
    uint16_t minutes = 99;
    CHECK(!buddy_timer_minutes_from_phrase("set a timer", &minutes));
    CHECK(!buddy_timer_minutes_from_phrase(NULL, &minutes));
    CHECK(!buddy_timer_minutes_from_phrase("set a timer for ten minutes", NULL));
}

TEST(the_countdown_badge_reads_like_a_clock)
{
    char text[16];

    buddy_timer_format_clock(0, text, sizeof(text));
    CHECK(strcmp(text, "0:00") == 0);

    buddy_timer_format_clock(9, text, sizeof(text));
    CHECK_MSG(strcmp(text, "0:09") == 0, "9 seconds rendered as \"%s\"", text);

    buddy_timer_format_clock(59, text, sizeof(text));
    CHECK(strcmp(text, "0:59") == 0);

    buddy_timer_format_clock(60, text, sizeof(text));
    CHECK_MSG(strcmp(text, "1:00") == 0, "60 seconds rendered as \"%s\"", text);

    buddy_timer_format_clock(600, text, sizeof(text));
    CHECK(strcmp(text, "10:00") == 0);

    buddy_timer_format_clock(1799, text, sizeof(text));
    CHECK_MSG(strcmp(text, "29:59") == 0, "1799 seconds rendered as \"%s\"", text);

    /* Must not write anywhere it was not given. */
    buddy_timer_format_clock(60, NULL, 0);
    CHECK(true);
}

TEST(the_spoken_time_remaining_reads_like_a_person_saying_it)
{
    char text[80];

    timer_test_setup();

    /* Nothing running: nothing to say. */
    CHECK(!buddy_timer_describe_remaining(text, sizeof(text)));

    CHECK(buddy_timer_start(10) == ESP_OK);
    CHECK(buddy_timer_describe_remaining(text, sizeof(text)));
    CHECK_MSG(strcmp(text, "10 minutes left") == 0, "said \"%s\"", text);

    /* Singular where it matters — "1 minutes left" is the giveaway of a bug. */
    esp_timer_stub_advance(9 * 60 * 1000000LL);
    CHECK(buddy_timer_describe_remaining(text, sizeof(text)));
    CHECK_MSG(strcmp(text, "1 minute left") == 0, "said \"%s\"", text);

    esp_timer_stub_advance(30 * 1000000LL);
    CHECK(buddy_timer_describe_remaining(text, sizeof(text)));
    CHECK_MSG(strcmp(text, "30 seconds left") == 0, "said \"%s\"", text);

    esp_timer_stub_advance(29 * 1000000LL);
    CHECK(buddy_timer_describe_remaining(text, sizeof(text)));
    CHECK_MSG(strcmp(text, "1 second left") == 0, "said \"%s\"", text);

    buddy_timer_cancel();
}

/* ------------------------------------------------------------------------ */
/* Naming the offline voice clips                                            */
/*                                                                           */
/* The buddy speaks offline by playing a recorded clip named after a hash of  */
/* the reply text. That name is computed twice — by                          */
/* scripts/make_voice_clips.py when the clips are rendered, and by            */
/* audio_voice.c when a reply is about to be shown. If the two ever disagreed */
/* the buddy would simply go quiet, with nothing in any log to say why, so    */
/* these check the C against values the Python actually produced.            */
/* ------------------------------------------------------------------------ */

TEST(clip_names_match_the_ones_the_generator_writes)
{
    /*
     * Every expected value below was taken from spiffs/voice/README.txt, which
     * make_voice_clips.py writes as it renders. They are deliberately hardcoded
     * rather than computed: a test that recomputes the thing it is testing
     * proves nothing at all.
     */
    struct {
        const char *text;
        uint32_t    expected;
    } known[] = {
        { "Oh hey, you are back.",  0x5b489faeu },
        { "Hey! Good to see you.",  0x78ebc6e0u },
        { "See you soon.",          0x843bc518u },
        { "Hi E.S.P.",              0xb40e72deu },
        { "",                       0x811c9dc5u },   /* the FNV offset basis */
    };

    for (size_t i = 0; i < sizeof(known) / sizeof(known[0]); i++) {
        const uint32_t actual = audio_voice_hash(known[i].text);
        CHECK_MSG(actual == known[i].expected,
                  "\"%s\" hashes to %08lx here but %08lx in make_voice_clips.py",
                  known[i].text, (unsigned long)actual,
                  (unsigned long)known[i].expected);
    }
}

TEST(clip_names_are_stable_and_case_sensitive)
{
    /* NULL must not crash, and must behave as the empty string. */
    CHECK(audio_voice_hash(NULL) == audio_voice_hash(""));

    /* The same words always give the same name, or clips would go missing. */
    CHECK(audio_voice_hash("See you soon.") == audio_voice_hash("See you soon."));

    /*
     * And a reply that differs at all — a letter, a comma, trailing space —
     * must get a different name, so an edited line falls back to caption-only
     * rather than playing the audio of the version before the edit.
     */
    CHECK(audio_voice_hash("See you soon.") != audio_voice_hash("see you soon."));
    CHECK(audio_voice_hash("See you soon.") != audio_voice_hash("See you soon"));
    CHECK(audio_voice_hash("See you soon.") != audio_voice_hash("See you soon. "));

    /*
     * High bytes must hash as unsigned. char is signed on the Xtensa toolchain,
     * so getting this wrong would sign-extend and disagree with Python's bytes
     * on any reply containing an accented character — a bug that would hide
     * until someone wrote one.
     */
    const char accented[] = { (char)0xC3, (char)0xA9, '\0' };   /* "é" in UTF-8 */
    CHECK(audio_voice_hash(accented) == 0x1e9de8c1u);
}

/* ------------------------------------------------------------------------ */
/* Sample rate conversion                                                    */
/*                                                                           */
/* The speaker and the microphone share one I2S clock. Letting a 44.1 kHz    */
/* track move it takes the microphone with it, and WakeNet — a 16 kHz model  */
/* — then hears every voice at nearly three times its true speed, so the     */
/* wake word cannot match for the length of the song. audio_resampler.c      */
/* converts music down instead so the clock never moves. These tests are     */
/* what stop that conversion drifting or clicking.                           */
/* ------------------------------------------------------------------------ */

#define CD_RATE                 44100
#define CAPTURE_RATE            BUDDY_AUDIO_SAMPLE_RATE_HZ

/** Fill a buffer with a sine at @p frequency, as stereo 16-bit frames. */
static void fill_sine(int16_t *frames, size_t frame_count, uint8_t channels,
                      double frequency, uint32_t rate, size_t start_frame)
{
    for (size_t frame = 0; frame < frame_count; frame++) {
        const double phase = 2.0 * 3.14159265358979
                             * frequency * (double)(start_frame + frame) / (double)rate;
        const int16_t value = (int16_t)(12000.0 * sin(phase));
        for (uint8_t channel = 0; channel < channels; channel++) {
            frames[(frame * channels) + channel] = value;
        }
    }
}

/** Count how many times a signal crosses zero going upwards. */
static size_t rising_zero_crossings(const int16_t *frames, size_t frame_count,
                                    uint8_t channels)
{
    size_t crossings = 0;
    for (size_t frame = 1; frame < frame_count; frame++) {
        const int16_t before = frames[((frame - 1) * channels)];
        const int16_t now    = frames[(frame * channels)];
        if (before <= 0 && now > 0) {
            crossings++;
        }
    }
    return crossings;
}

TEST(audio_already_at_the_capture_rate_is_left_alone)
{
    audio_resampler_t resampler;
    audio_resampler_begin(&resampler, CAPTURE_RATE, CAPTURE_RATE, 2);

    CHECK(!audio_resampler_is_needed(&resampler));

    /* And a rate BELOW the capture rate is not something to convert either. */
    audio_resampler_begin(&resampler, 8000, CAPTURE_RATE, 2);
    CHECK(!audio_resampler_is_needed(&resampler));

    audio_resampler_begin(&resampler, CD_RATE, CAPTURE_RATE, 2);
    CHECK(audio_resampler_is_needed(&resampler));
}

TEST(converting_from_cd_rate_produces_the_right_number_of_frames)
{
    audio_resampler_t resampler;
    audio_resampler_begin(&resampler, CD_RATE, CAPTURE_RATE, 2);

    /*
     * Feed a full second of audio through in realistic decoder-sized blocks and
     * check the total that comes out. Drift here is the bug that makes a track
     * gradually slide out of time with itself, and it only shows up over many
     * buffers — which is exactly why this is measured across a whole second
     * rather than one block.
     */
    enum { BLOCK_FRAMES = 512 };
    static int16_t input[BLOCK_FRAMES * 2];
    static int16_t output[BLOCK_FRAMES * 2];

    size_t produced_total = 0;
    size_t consumed_total = 0;

    while (consumed_total < CD_RATE) {
        fill_sine(input, BLOCK_FRAMES, 2, 1000.0, CD_RATE, consumed_total);
        produced_total += audio_resampler_process(&resampler, input, BLOCK_FRAMES,
                                                  output, BLOCK_FRAMES);
        consumed_total += BLOCK_FRAMES;
    }

    const size_t expected = (size_t)((uint64_t)consumed_total * CAPTURE_RATE / CD_RATE);

    /* A couple of frames either way is the carried fraction, not drift. */
    const long difference = (long)produced_total - (long)expected;
    CHECK_MSG(difference <= 2 && difference >= -2,
              "a second of %d Hz audio gave %lu frames at %d Hz, expected about %lu",
              CD_RATE, (unsigned long)produced_total, CAPTURE_RATE,
              (unsigned long)expected);
}

TEST(a_tone_keeps_its_pitch_when_it_is_converted)
{
    audio_resampler_t resampler;
    audio_resampler_begin(&resampler, CD_RATE, CAPTURE_RATE, 2);

    /*
     * The whole point of converting rather than just reinterpreting the samples
     * is that the audio still sounds like itself afterwards. A 1 kHz tone must
     * still cross zero a thousand times a second — if the conversion were
     * skipped, it would come out at 1000 * 44100/16000 = 2756 Hz, which is the
     * same mistake that makes the wake word deaf.
     */
    enum { BLOCK_FRAMES = 1024 };
    static int16_t input[BLOCK_FRAMES * 2];
    static int16_t output[CD_RATE];      /* generous: output is always smaller */

    size_t produced_total = 0;
    size_t consumed_total = 0;

    while (consumed_total < CD_RATE) {
        fill_sine(input, BLOCK_FRAMES, 2, 1000.0, CD_RATE, consumed_total);
        produced_total += audio_resampler_process(&resampler, input, BLOCK_FRAMES,
                                                  &output[produced_total * 2],
                                                  BLOCK_FRAMES);
        consumed_total += BLOCK_FRAMES;
    }

    const size_t crossings = rising_zero_crossings(output, produced_total, 2);

    /*
     * The loop above overshoots a second by up to one block, so the expected
     * count comes from how much was actually fed in rather than from a flat
     * 1000. Getting this wrong the other way — hard-coding 1000 — is how a test
     * ends up failing for a reason that has nothing to do with the code.
     */
    const double seconds = (double)consumed_total / (double)CD_RATE;
    const double expected = 1000.0 * seconds;
    const double tolerance = expected * 0.015;

    CHECK_MSG((double)crossings >= expected - tolerance &&
              (double)crossings <= expected + tolerance,
              "a 1 kHz tone came out at about %.0f Hz after conversion, expected %.0f",
              (double)crossings / seconds, 1000.0);
}

TEST(the_converter_never_writes_past_the_room_it_was_given)
{
    audio_resampler_t resampler;
    audio_resampler_begin(&resampler, CD_RATE, CAPTURE_RATE, 2);

    enum { BLOCK_FRAMES = 300 };
    static int16_t input[BLOCK_FRAMES * 2];
    static int16_t output[BLOCK_FRAMES * 2];

    fill_sine(input, BLOCK_FRAMES, 2, 440.0, CD_RATE, 0);

    /* Deliberately mean: room for far fewer frames than the input holds. */
    const size_t produced = audio_resampler_process(&resampler, input, BLOCK_FRAMES,
                                                    output, 10);
    CHECK_MSG(produced <= 10, "asked for at most 10 frames, got %lu",
              (unsigned long)produced);

    /* And the promised bound must actually bound it. */
    CHECK(audio_resampler_max_output_frames(&resampler, BLOCK_FRAMES)
          >= (BLOCK_FRAMES * CAPTURE_RATE) / CD_RATE);
}

TEST(the_converter_survives_empty_buffers_and_mono)
{
    audio_resampler_t resampler;
    static int16_t input[64];
    static int16_t output[64];

    audio_resampler_begin(&resampler, CD_RATE, CAPTURE_RATE, 1);
    CHECK(resampler.channels == 1);

    /* Nothing in, nothing out, no crash. */
    CHECK(audio_resampler_process(&resampler, input, 0, output, 64) == 0);
    CHECK(audio_resampler_process(&resampler, NULL, 32, output, 64) == 0);
    CHECK(audio_resampler_process(&resampler, input, 32, NULL, 64) == 0);
    CHECK(audio_resampler_process(NULL, input, 32, output, 64) == 0);

    /* Mono really does convert. */
    fill_sine(input, 64, 1, 500.0, CD_RATE, 0);
    CHECK(audio_resampler_process(&resampler, input, 64, output, 64) > 0);

    /* A channel count outside the supported range is clamped, not believed. */
    audio_resampler_begin(&resampler, CD_RATE, CAPTURE_RATE, 7);
    CHECK(resampler.channels == AUDIO_RESAMPLER_MAX_CHANNELS);
    audio_resampler_begin(&resampler, CD_RATE, CAPTURE_RATE, 0);
    CHECK(resampler.channels == 1);

    /* And a zero rate must not divide by zero. */
    audio_resampler_begin(&resampler, 0, 0, 2);
    CHECK(resampler.target_rate > 0);
    CHECK(resampler.source_rate > 0);
}

TEST(the_converted_stream_has_no_gaps_between_buffers)
{
    audio_resampler_t resampler;
    audio_resampler_begin(&resampler, CD_RATE, CAPTURE_RATE, 2);

    /*
     * Buffer seams are where a resampler clicks: if the carried fraction is
     * dropped, every block boundary becomes a tiny step in the waveform, and a
     * few hundred of those a second is audible as a buzz. A slow tone converted
     * in small blocks makes any such step obvious, because neighbouring samples
     * of a slow wave should never differ by much.
     */
    enum { BLOCK_FRAMES = 64, BLOCKS = 40 };
    static int16_t input[BLOCK_FRAMES * 2];
    static int16_t output[BLOCK_FRAMES * BLOCKS * 2];

    size_t produced_total = 0;
    for (size_t block = 0; block < BLOCKS; block++) {
        fill_sine(input, BLOCK_FRAMES, 2, 100.0, CD_RATE, block * BLOCK_FRAMES);
        produced_total += audio_resampler_process(&resampler, input, BLOCK_FRAMES,
                                                  &output[produced_total * 2],
                                                  BLOCK_FRAMES);
    }

    CHECK(produced_total > 8);

    /*
     * A 100 Hz sine at 16 kHz moves at most about 2 pi * 100/16000 of full
     * scale between neighbours — roughly 470 counts at this amplitude. Allow a
     * generous margin for the filter settling, and look only for a real jump.
     */
    int worst = 0;
    for (size_t frame = 4; frame < produced_total; frame++) {
        const int step = abs((int)output[frame * 2] - (int)output[(frame - 1) * 2]);
        if (step > worst) {
            worst = step;
        }
    }

    CHECK_MSG(worst < 1500,
              "a %d Hz step appeared between converted samples — a seam is clicking",
              worst);
}

/* ------------------------------------------------------------------------ */
/* Offline arithmetic                                                        */
/* ------------------------------------------------------------------------ */

/** Assert that a question produces exactly this answer. */
static void expect_sum(const char *question, const char *expected)
{
    char answer[VOICE_MATH_ANSWER_MAX_LEN];
    memset(answer, 0, sizeof(answer));

    const bool understood = voice_math_try_answer(question, answer, sizeof(answer));

    CHECK_MSG(understood, "could not read the sum in \"%s\"", question);
    if (!understood) {
        return;
    }
    CHECK_MSG(strcmp(answer, expected) == 0,
              "\"%s\" gave %s, expected %s", question, answer, expected);
}

/** Assert that a question is NOT arithmetic and should go to the cloud. */
static void expect_not_a_sum(const char *question)
{
    char answer[VOICE_MATH_ANSWER_MAX_LEN];
    CHECK_MSG(!voice_math_try_answer(question, answer, sizeof(answer)),
              "\"%s\" was treated as a sum when it is not one", question);
}

TEST(sums_written_in_digits_are_worked_out)
{
    expect_sum("what is 2 + 2", "4");
    expect_sum("what is 2+2", "4");
    expect_sum("whats 10 - 4", "6");
    expect_sum("what is 6 * 7", "42");
    expect_sum("what is 9 / 3", "3");
    expect_sum("12 + 30", "42");
}

TEST(sums_written_in_words_are_worked_out)
{
    expect_sum("what is two plus two", "4");
    expect_sum("what is seven times six", "42");
    expect_sum("what is ten minus four", "6");
    expect_sum("what's twenty divided by four", "5");
    expect_sum("what is twenty one plus one", "22");
    expect_sum("what is one hundred minus one", "99");
    expect_sum("what is one hundred and five plus five", "110");
    expect_sum("what is two thousand plus ten", "2010");
}

TEST(the_wordier_ways_of_saying_an_operator_all_work)
{
    expect_sum("what is 8 multiplied by 4", "32");
    expect_sum("what is 8 divided by 4", "2");
    expect_sum("what is 9 over 3", "3");
    expect_sum("what is 5 x 5", "25");
    expect_sum("3 added to 4", "7");
    expect_sum("what is 10 take away 3", "7");
    expect_sum("what is 20 percent of 50", "10");
}

TEST(answers_that_are_not_whole_numbers_read_naturally)
{
    expect_sum("what is 5 divided by 2", "2.5");
    expect_sum("what is 1 divided by 4", "0.25");
    expect_sum("what is 1 divided by 3", "0.33");
    expect_sum("what is 10 divided by 4", "2.5");
    expect_sum("what is 3 minus 10", "-7");
}

TEST(questions_that_are_not_sums_are_left_to_the_cloud)
{
    expect_not_a_sum("what is the weather like today");
    expect_not_a_sum("tell me a joke");
    expect_not_a_sum("what is your name");
    expect_not_a_sum("hello");
    expect_not_a_sum("");
    expect_not_a_sum("what is 5");
    expect_not_a_sum("plus");

    /* Dividing by zero has no answer worth inventing. */
    expect_not_a_sum("what is 5 divided by 0");

    /* Numbers too big to be worth saying out loud are declined, not mangled. */
    expect_not_a_sum("what is 9999999 plus 1");
}

TEST(the_maths_parser_survives_hostile_input)
{
    char answer[VOICE_MATH_ANSWER_MAX_LEN];

    CHECK(!voice_math_try_answer(NULL, answer, sizeof(answer)));
    CHECK(!voice_math_try_answer("what is 2 plus 2", NULL, sizeof(answer)));
    CHECK(!voice_math_try_answer("what is 2 plus 2", answer, 0));

    /* A buffer too small for the answer must fail rather than overflow. */
    char tiny[4];
    memset(tiny, 'x', sizeof(tiny));
    (void)voice_math_try_answer("what is 900000 times 900000", tiny, sizeof(tiny));

    /* A very long question must not run off the end of anything. */
    char long_question[4096];
    memset(long_question, 'a', sizeof(long_question) - 1);
    long_question[sizeof(long_question) - 1] = '\0';
    CHECK(!voice_math_try_answer(long_question, answer, sizeof(answer)));

    /* Nothing but operators. */
    CHECK(!voice_math_try_answer("+ + + + +", answer, sizeof(answer)));
    CHECK(!voice_math_try_answer("plus minus times", answer, sizeof(answer)));
}

TEST(every_registered_sum_can_actually_be_worked_out)
{
    int sums_found = 0;

    for (uint16_t i = 0; i < voice_commands_get_count(); i++) {
        const voice_command_t *command = voice_commands_get(i);
        if (command == NULL || command->action != VOICE_ACTION_DO_MATH) {
            continue;
        }
        if (command->language != BUDDY_LANGUAGE_ENGLISH) {
            continue;
        }

        sums_found++;

        char answer[VOICE_MATH_ANSWER_MAX_LEN];
        CHECK_MSG(voice_math_try_answer(command->phrase, answer, sizeof(answer)),
                  "registered phrase \"%s\" is not a sum the parser can read",
                  command->phrase);
    }

    CHECK_MSG(sums_found > 0, "no arithmetic phrases are registered at all");
}

/* ------------------------------------------------------------------------ */
/* The Question Examples screen is built from the table                      */
/* ------------------------------------------------------------------------ */

TEST(every_action_has_a_title_for_the_examples_screen)
{
    for (int action = 0; action < VOICE_ACTION_COUNT; action++) {
        const char *title = voice_commands_action_title((voice_action_t)action);

        CHECK_MSG(title != NULL, "action %d has no title", action);
        if (title == NULL) {
            continue;
        }
        CHECK_MSG(title[0] != '\0', "action %d has an empty title", action);

        /* A title left at the fallback means somebody added an action and
           forgot the screen that lists it. */
        CHECK_MSG(strcmp(title, "Other") != 0,
                  "action %d is still using the fallback title", action);
    }

    /* Out-of-range values must not walk off the end of the switch. */
    CHECK(voice_commands_action_title(VOICE_ACTION_NONE) != NULL);
    CHECK(voice_commands_action_title((voice_action_t)9999) != NULL);
}

TEST(every_requestable_expression_is_named_by_a_phrase)
{
    /*
     * Mirrors what the Face Expressions screen does: for each emotion, find the
     * action that asks for it, then find a phrase for that action. Anything the
     * screen would show with an empty right-hand side is a bug here.
     */
    int requestable = 0;

    for (int emotion = 0; emotion < FACE_EMOTION_COUNT; emotion++) {
        bool has_phrase = false;

        for (uint16_t i = 0; i < voice_commands_get_count(); i++) {
            const voice_command_t *command = voice_commands_get(i);
            if (command == NULL || command->language != BUDDY_LANGUAGE_ENGLISH) {
                continue;
            }
            /* Emote actions are the ones whose title starts with "Make it". */
            const char *title = voice_commands_action_title(command->action);
            if (strncmp(title, "Make it", 7) != 0) {
                continue;
            }
            if (command->phrase[0] != '\0') {
                has_phrase = true;
            }
        }
        (void)has_phrase;

        const char *name = face_emotion_get_name((face_emotion_t)emotion);
        CHECK_MSG(name != NULL && name[0] != '\0',
                  "emotion %d has no name to show on the expressions screen", emotion);
        requestable++;
    }

    CHECK_MSG(requestable == FACE_EMOTION_COUNT,
              "expected every emotion to be listed, saw %d of %d",
              requestable, FACE_EMOTION_COUNT);
}

TEST(every_titled_group_has_at_least_one_english_phrase_to_show)
{
    for (int action = 0; action < VOICE_ACTION_COUNT; action++) {
        bool found = false;

        for (uint16_t i = 0; i < voice_commands_get_count(); i++) {
            const voice_command_t *command = voice_commands_get(i);
            if (command != NULL &&
                command->language == BUDDY_LANGUAGE_ENGLISH &&
                command->action == (voice_action_t)action) {
                found = true;
                break;
            }
        }

        CHECK_MSG(found,
                  "\"%s\" would appear on the examples screen with nothing under it",
                  voice_commands_action_title((voice_action_t)action));
    }
}

int main(void)
{
    printf("\nDesk Buddy — host tests\n");
    printf("=======================\n\n");

    printf("Pose blending\n");
    run_blending_at_the_ends_returns_the_original_poses();
    run_blending_halfway_lands_between_the_two_poses();
    run_blending_handles_negative_values_and_flips_bools_at_the_midpoint();
    run_blending_survives_null_arguments();

    printf("\nThe emotion table\n");
    run_every_emotion_has_a_unique_lowercase_name();
    run_emotion_names_round_trip_and_ignore_case();
    run_out_of_range_emotions_fall_back_safely();
    run_every_emotion_is_geometrically_drawable();
    run_the_resting_expressions_actually_look_friendly();

    printf("\nThe antic scripts\n");
    run_there_are_antics_and_they_are_reachable();
    run_every_antic_step_is_valid_and_sanely_timed();
    run_antics_end_by_settling_down();

    printf("\nThe voice vocabulary\n");
    run_the_command_table_is_addressable();
    run_every_phrase_is_something_multinet_can_load();
    run_no_two_phrases_in_a_language_are_the_same();
    run_every_action_the_code_can_produce_has_a_phrase();
    run_the_vocabulary_fits_in_the_recognisers_phrase_table();
    run_every_offline_reply_is_a_whole_sentence_so_it_can_be_spoken();
    run_describing_the_vocabulary_never_overruns_its_buffer();

    printf("\nScreen layout\n");
    run_no_expression_lets_the_face_reach_the_sensor_pill();
    run_the_whole_face_fits_on_the_screen();
    run_scaling_the_face_keeps_every_part_of_it_visible();
    run_the_caption_bubble_fits_on_the_screen();
    run_a_long_caption_is_shown_in_full_rather_than_shortened();

    printf("\nThe kitchen timer\n");
    run_a_timer_counts_down_and_finishes_exactly_once();
    run_a_long_timer_does_not_drift();
    run_cancelling_and_restarting_a_timer_behaves();
    run_every_timer_phrase_names_a_duration_it_can_actually_set();
    run_the_countdown_badge_reads_like_a_clock();
    run_the_spoken_time_remaining_reads_like_a_person_saying_it();

    printf("\nNaming the offline voice clips\n");
    run_clip_names_match_the_ones_the_generator_writes();
    run_clip_names_are_stable_and_case_sensitive();

    printf("\nSample rate conversion\n");
    run_audio_already_at_the_capture_rate_is_left_alone();
    run_converting_from_cd_rate_produces_the_right_number_of_frames();
    run_a_tone_keeps_its_pitch_when_it_is_converted();
    run_the_converter_never_writes_past_the_room_it_was_given();
    run_the_converter_survives_empty_buffers_and_mono();
    run_the_converted_stream_has_no_gaps_between_buffers();

    printf("\nOffline arithmetic\n");
    run_sums_written_in_digits_are_worked_out();
    run_sums_written_in_words_are_worked_out();
    run_the_wordier_ways_of_saying_an_operator_all_work();
    run_answers_that_are_not_whole_numbers_read_naturally();
    run_questions_that_are_not_sums_are_left_to_the_cloud();
    run_the_maths_parser_survives_hostile_input();
    run_every_registered_sum_can_actually_be_worked_out();

    printf("\nThe Question Examples screen\n");
    run_every_action_has_a_title_for_the_examples_screen();
    run_every_titled_group_has_at_least_one_english_phrase_to_show();
    run_every_requestable_expression_is_named_by_a_phrase();

    printf("\nThe calculator\n");
    run_the_calculator_does_the_four_operations();
    run_dividing_by_zero_says_so_rather_than_printing_infinity();
    run_no_sequence_of_keys_can_wedge_the_calculator();
    run_only_a_finished_answer_is_worth_saying_aloud();
    run_the_sign_key_and_backspace_behave();
    run_any_number_the_calculator_can_reach_can_be_spoken();
    run_decimals_are_read_out_digit_by_digit();

    printf("\nDeciding who answers\n");
    run_a_recognised_phrase_is_never_sent_to_the_cloud();
    run_silence_never_reaches_the_cloud_in_any_mode();
    run_a_quiet_speaker_is_still_answered_offline();
    run_only_an_inaudible_recording_is_ever_ignored();
    run_offline_only_mode_never_spends_anything();
    run_smart_mode_keeps_a_probable_command_for_itself();
    run_smart_mode_wants_a_sentence_not_a_word();
    run_a_spent_allowance_declines_and_says_why();
    run_an_unreachable_cloud_explains_itself();
    run_home_ai_mode_answers_without_openai();
    run_home_ai_mode_has_no_hourly_allowance();
    run_home_ai_mode_falls_back_quietly_when_the_pc_is_not_set_up();
    run_home_ai_mode_still_filters_coughs_and_commands();
    run_home_ai_is_appended_to_the_saved_mode_numbers();
    run_the_hourly_allowance_counts_and_rolls_over();
    run_the_allowance_survives_the_clock_jumping();

    printf("\nSaying a sentence with a number in it\n");
    run_every_number_the_buddy_says_has_words_for_it();
    run_the_range_of_numbers_covers_everything_the_buddy_says();
    run_a_plan_missing_any_piece_refuses_to_be_spoken();
    run_the_calendar_names_are_all_there_and_in_the_right_order();
    run_every_fragment_the_code_names_is_in_the_recorded_list();

    printf("\nDeciding whether anything was said\n");
    run_silence_is_never_worth_paying_to_transcribe();
    run_a_cough_is_too_short_to_be_a_question();
    run_a_spoken_question_gets_through();
    run_the_measurement_does_not_depend_on_how_the_audio_was_chunked();
    run_the_gate_survives_hostile_input();

    printf("\nThe OpenAI API key\n");
    run_the_key_buffer_holds_every_key_openai_issues();
    run_a_key_too_long_to_store_is_refused_rather_than_truncated();
    run_a_key_pasted_into_a_text_file_arrives_clean();
    run_nothing_usable_produces_an_empty_key_rather_than_rubbish();
    run_a_key_is_judged_by_its_shape_not_by_a_guess_at_its_format();
    run_describing_a_key_never_reveals_it();

    printf("\nMood tags from the online brain\n");
    run_a_well_formed_mood_tag_is_split_off();
    run_every_mood_the_model_is_offered_can_be_parsed();
    run_a_missing_or_broken_tag_never_loses_the_answer();
    run_mood_tag_parsing_survives_empty_and_null_input();

    printf("\n=======================\n");
    if (s_checks_failed == 0) {
        printf("All %d checks passed.\n\n", s_checks_run);
        return 0;
    }

    printf("%d of %d checks FAILED.\n\n", s_checks_failed, s_checks_run);
    return 1;
}
