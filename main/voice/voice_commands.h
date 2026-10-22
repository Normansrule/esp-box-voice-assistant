/*
 * voice_commands.h — The phrases the buddy recognises without any internet.
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 *
 * HOW SPEECH RECOGNITION WORKS ON THIS CHIP
 * -----------------------------------------
 * The ESP32-S3 runs two small neural networks locally:
 *
 *   WakeNet   listens continuously for one fixed wake word ("Hi E.S.P.").
 *             It is tiny and always on.
 *
 *   MultiNet  runs only after the wake word fires. It compares what it hears
 *             against a LIST OF PHRASES YOU REGISTER IN ADVANCE. It cannot
 *             transcribe arbitrary speech — it can only decide which of your
 *             phrases (if any) was said.
 *
 * That is why this file exists: it is the complete vocabulary of the offline
 * buddy. Anything not on this list falls through to the cloud brain, which can
 * handle free-form questions.
 *
 * The English model wants plain text; the Chinese model wants pinyin. The table
 * carries both so switching languages is a settings change, not a code change.
 */

#pragma once

#include <stddef.h>
#include <stdint.h>
#include "buddy_settings.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Longest a spoken phrase or its phonetic spelling may be. */
#define VOICE_PHRASE_MAX_LEN        64

/**
 * @brief What the buddy should DO when a phrase is recognised.
 *
 * Several phrases map onto the same action — "play some music" and "play a
 * song" both mean VOICE_ACTION_MUSIC_PLAY — which makes the buddy feel less
 * finicky about exact wording.
 */
typedef enum {
    /* Conversation */
    VOICE_ACTION_GREETING = 0,      /*!< "hello buddy"                        */
    VOICE_ACTION_HOW_ARE_YOU,       /*!< "how are you"                        */
    VOICE_ACTION_WHAT_CAN_YOU_DO,   /*!< "what can you do"                    */
    VOICE_ACTION_TELL_JOKE,         /*!< "tell me a joke"                     */
    VOICE_ACTION_WHO_MADE_YOU,      /*!< "who made you"                       */
    VOICE_ACTION_GOODBYE,           /*!< "goodbye" / "go to sleep"            */

    /* Information the buddy can answer from its own hardware */
    VOICE_ACTION_TELL_TIME,         /*!< "what time is it"                    */
    VOICE_ACTION_TELL_DATE,         /*!< "what is the date"                   */
    VOICE_ACTION_TELL_TEMPERATURE,  /*!< "what is the temperature"            */
    VOICE_ACTION_TELL_HUMIDITY,     /*!< "how humid is it"                    */

    /*
     * Arithmetic. Unusual among the actions here in that the ANSWER depends on
     * the exact phrase that matched, not just on which action it maps to — the
     * local brain re-reads the phrase and works the sum out. See voice_math.h.
     */
    VOICE_ACTION_DO_MATH,           /*!< "what is two plus two"               */

    /* Music */
    VOICE_ACTION_MUSIC_PLAY,        /*!< "play some music"                    */
    VOICE_ACTION_MUSIC_NEXT,        /*!< "next song"                          */
    VOICE_ACTION_MUSIC_STOP,        /*!< "stop the music"                     */

    /* Expressions the user can request directly */
    VOICE_ACTION_EMOTE_HAPPY,       /*!< "be happy"                           */
    VOICE_ACTION_EMOTE_SAD,         /*!< "be sad"                             */
    VOICE_ACTION_EMOTE_SURPRISED,   /*!< "act surprised"                      */
    VOICE_ACTION_EMOTE_WINK,        /*!< "give me a wink"                     */
    VOICE_ACTION_EMOTE_SILLY,       /*!< "be silly" — random antic            */
    VOICE_ACTION_EMOTE_EXCITED,     /*!< "get excited now"                    */
    VOICE_ACTION_EMOTE_CURIOUS,     /*!< "look really curious"                */
    VOICE_ACTION_EMOTE_SLEEPY,      /*!< "act sleepy for me"                  */
    VOICE_ACTION_EMOTE_LOVE,        /*!< "show me some love"                  */
    VOICE_ACTION_EMOTE_CONFUSED,    /*!< "look confused now"                  */
    VOICE_ACTION_EMOTE_DIZZY,       /*!< "make your eyes spin"                */
    VOICE_ACTION_EMOTE_YAWN,        /*!< "give me a big yawn"                 */
    VOICE_ACTION_EMOTE_ANNOYED,     /*!< "pull a grumpy face"                 */
    VOICE_ACTION_EMOTE_MISCHIEF,    /*!< "look a bit sneaky"                  */
    VOICE_ACTION_EMOTE_PROUD,       /*!< "puff your chest out"                */
    VOICE_ACTION_EMOTE_BORED,       /*!< "pretend to be bored"                */
    VOICE_ACTION_EMOTE_SMUG,        /*!< "look pleased with yourself"         */
    VOICE_ACTION_EMOTE_SHOCKED,     /*!< "act totally shocked"                */
    VOICE_ACTION_EMOTE_DETERMINED,  /*!< "look determined now"                */
    VOICE_ACTION_EMOTE_GIGGLY,      /*!< "have a little giggle"               */
    VOICE_ACTION_EMOTE_SUSPICIOUS,  /*!< "look suspicious of me"              */
    VOICE_ACTION_EMOTE_RELIEVED,    /*!< "act relieved now"                   */
    VOICE_ACTION_EMOTE_AMAZED,      /*!< "look totally amazed"                */
    VOICE_ACTION_EMOTE_NERVOUS,     /*!< "act a bit nervous"                  */
    VOICE_ACTION_EMOTE_PLEADING,    /*!< "give me puppy eyes"                 */
    VOICE_ACTION_EMOTE_TRIUMPHANT,  /*!< "celebrate with me"                  */
    VOICE_ACTION_EMOTE_UNIMPRESSED, /*!< "look unimpressed"                   */
    VOICE_ACTION_EMOTE_SCRUNCH,     /*!< "scrunch your face up"               */
    VOICE_ACTION_EMOTE_SHY,         /*!< "act all shy now"                    */
    VOICE_ACTION_EMOTE_PANIC,       /*!< "start panicking now"                */
    VOICE_ACTION_EMOTE_DISGUSTED,   /*!< "look totally disgusted"             */
    VOICE_ACTION_EMOTE_SINGING,     /*!< "sing along with me"                 */
    /* Expressions added in v3.5 */
    VOICE_ACTION_EMOTE_STARRY,      /*!< "look amazed at me"                  */
    VOICE_ACTION_EMOTE_SCEPTICAL,   /*!< "give me a doubtful look"            */
    VOICE_ACTION_EMOTE_SHEEPISH,    /*!< "look a bit guilty"                  */
    VOICE_ACTION_EMOTE_HOPEFUL,     /*!< "look hopeful now"                   */
    VOICE_ACTION_EMOTE_THOUGHTFUL,  /*!< "have a little think"                */
    VOICE_ACTION_EMOTE_CHEEKY,      /*!< "be cheeky for me"                   */
    VOICE_ACTION_EMOTE_ALARMED,     /*!< "look startled now"                  */
    VOICE_ACTION_EMOTE_COSY,        /*!< "get comfortable"                    */
    VOICE_ACTION_EMOTE_DREAMY,      /*!< "daydream a little"                  */
    VOICE_ACTION_EMOTE_FOCUSED,     /*!< "concentrate very hard"              */
    VOICE_ACTION_EMOTE_PLAYFUL,     /*!< "be playful now"                     */
    VOICE_ACTION_EMOTE_EXHAUSTED,   /*!< "look worn out"                      */

    VOICE_ACTION_EMOTE_NEUTRAL,     /*!< "go back to normal"                  */

    /* Kitchen timer — offline, and the most useful thing on a desk */
    VOICE_ACTION_TIMER_SET,         /*!< "set a timer for five minutes"       */
    VOICE_ACTION_TIMER_CHECK,       /*!< "how long is left"                   */
    VOICE_ACTION_TIMER_CANCEL,      /*!< "cancel the timer"                   */

    /* Deciding things for you */
    VOICE_ACTION_FLIP_COIN,         /*!< "flip a coin"                        */
    VOICE_ACTION_ROLL_DICE,         /*!< "roll a dice"                        */
    VOICE_ACTION_ROCK_PAPER_SCISSORS, /*!< "rock paper scissors"              */
    VOICE_ACTION_ORACLE,            /*!< "decide for me" — a yes/no answer    */

    /*
     * Conversation that costs nothing to run.
     *
     * Every one of these answers from a fixed sentence, which is what lets the
     * offline voice speak it: scripts/make_voice_clips.py records each reply in
     * this file at build time and audio_voice.c finds it again by content hash.
     * A reply assembled at runtime ("it is %d degrees") has no fixed text and
     * so stays silent, which is why these are written out in full.
     */
    VOICE_ACTION_YOUR_NAME,         /*!< "what is your name"                  */
    VOICE_ACTION_HOW_OLD_ARE_YOU,   /*!< "how old are you"                    */
    VOICE_ACTION_ARE_YOU_A_ROBOT,   /*!< "are you a robot"                    */
    VOICE_ACTION_THANK_YOU,         /*!< "thank you buddy"                    */
    VOICE_ACTION_COMPLIMENT,        /*!< "you are the best"                   */
    VOICE_ACTION_GOOD_MORNING,      /*!< "good morning buddy"                 */
    VOICE_ACTION_HIGH_FIVE,         /*!< "give me a high five"                */
    VOICE_ACTION_TELL_FACT,         /*!< "tell me a fact"                     */
    VOICE_ACTION_COUNT_TO_TEN,      /*!< "count to ten"                       */

    /*
     * Questions about itself, added in v3.5. Every one is answered from
     * something the chip already knows, and every one is SPOKEN — the numeric
     * ones by assembling fragments. See voice_speech_parts.h.
     */
    VOICE_ACTION_HOW_LONG_AWAKE,    /*!< "how long have you been on"          */
    VOICE_ACTION_HOW_IS_YOUR_MEMORY,/*!< "how is your memory"                 */
    VOICE_ACTION_NETWORK_STATUS,    /*!< "are you online"                     */
    VOICE_ACTION_WHAT_IS_WAKE_WORD, /*!< "what is your wake word"             */
    VOICE_ACTION_GIVE_ADVICE,       /*!< "give me some advice"                */
    VOICE_ACTION_TELL_FORTUNE,      /*!< "read my fortune"                    */
    VOICE_ACTION_TELL_STORY,        /*!< "tell me a story"                    */
    VOICE_ACTION_ENCOURAGE_ME,      /*!< "cheer me up"                        */

    /* Device control */
    VOICE_ACTION_VOLUME_UP,
    VOICE_ACTION_VOLUME_DOWN,
    VOICE_ACTION_OPEN_SETTINGS,
    VOICE_ACTION_OPEN_CALCULATOR,   /*!< "open the calculator"                */

    VOICE_ACTION_COUNT,
    VOICE_ACTION_NONE,              /*!< Nothing matched                      */
} voice_action_t;

/**
 * @brief One row of the vocabulary table.
 */
typedef struct {
    voice_action_t   action;
    buddy_language_t language;
    char             phrase[VOICE_PHRASE_MAX_LEN];   /*!< Shown on screen     */
    char             phonetic[VOICE_PHRASE_MAX_LEN]; /*!< Fed to the model    */
} voice_command_t;

/**
 * @brief How many rows the vocabulary table has, across all languages.
 */
uint16_t voice_commands_get_count(void);

/**
 * @brief Fetch one row of the vocabulary table.
 *
 * @return Pointer to a constant row, or NULL when the index is out of range.
 */
const voice_command_t *voice_commands_get(uint16_t index);

/**
 * @brief Build a short, human-readable list of example phrases.
 *
 * Used by the "what can you do" answer and by the settings screen, so the list
 * shown to the user can never drift out of step with what actually works.
 *
 * @param language     Which language's phrases to list.
 * @param buffer       Destination text buffer.
 * @param buffer_size  Size of @p buffer in bytes.
 */
void voice_commands_describe(buddy_language_t language, char *buffer, size_t buffer_size);

/**
 * @brief A short human-readable title for what an action does.
 *
 * Used as the group heading on the Question Examples screen, so that the list
 * the user reads is built from the same table the recogniser is loaded from and
 * cannot fall out of step with it.
 *
 * @param action  Any value of voice_action_t.
 * @return A constant string, never NULL — unknown actions get a safe fallback.
 */
const char *voice_commands_action_title(voice_action_t action);

#ifdef __cplusplus
}
#endif
