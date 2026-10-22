/*
 * voice_commands.c — The offline vocabulary.
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 *
 * ADDING A PHRASE
 * ---------------
 * 1. Add a row to the table below with the action it should trigger.
 * 2. If the action is new, add it to voice_action_t in the header and handle it
 *    in voice_local_brain.c.
 * That is the whole procedure — nothing else needs touching.
 *
 * TIPS FOR PHRASES THAT ACTUALLY GET RECOGNISED
 * ---------------------------------------------
 *  - Three to five syllables works best. One-word commands misfire constantly.
 *  - Avoid two phrases that rhyme; the recogniser will confuse them.
 *  - Give every action two or three wordings so users need not memorise one.
 *
 * The English model (MultiNet 6) takes plain lowercase text, so `phrase` and
 * `phonetic` are the same for English rows. The Chinese model takes pinyin
 * without tone marks, which is what the Chinese rows put in `phonetic`.
 */

#include <stdio.h>
#include <string.h>

#include "voice_commands.h"

/* Keeps the table below narrow enough to read at a glance. */
#define EN(action, text)              { action, BUDDY_LANGUAGE_ENGLISH, text, text }
#define CN(action, text, pinyin)      { action, BUDDY_LANGUAGE_CHINESE, text, pinyin }

static const voice_command_t s_commands[] = {

    /* ---------------- English -------------------------------------------- */

    /* Small talk */
    EN(VOICE_ACTION_GREETING,          "hello buddy"),
    EN(VOICE_ACTION_GREETING,          "hey there little guy"),
    EN(VOICE_ACTION_HOW_ARE_YOU,       "how are you doing"),
    EN(VOICE_ACTION_HOW_ARE_YOU,       "how are you today"),
    EN(VOICE_ACTION_WHAT_CAN_YOU_DO,   "what can you do"),
    EN(VOICE_ACTION_WHAT_CAN_YOU_DO,   "help me please"),
    EN(VOICE_ACTION_TELL_JOKE,         "tell me a joke"),
    EN(VOICE_ACTION_TELL_JOKE,         "make me laugh"),
    EN(VOICE_ACTION_WHO_MADE_YOU,      "who made you"),
    EN(VOICE_ACTION_GOODBYE,           "goodbye buddy"),
    EN(VOICE_ACTION_GOODBYE,           "go to sleep now"),

    /* Things the buddy can answer from its own sensors and clock */
    EN(VOICE_ACTION_TELL_TIME,         "what time is it"),
    EN(VOICE_ACTION_TELL_TIME,         "tell me the time"),
    EN(VOICE_ACTION_TELL_DATE,         "what is the date"),
    EN(VOICE_ACTION_TELL_DATE,         "what day is it"),
    EN(VOICE_ACTION_TELL_TEMPERATURE,  "what is the temperature"),
    EN(VOICE_ACTION_TELL_TEMPERATURE,  "how warm is it"),
    EN(VOICE_ACTION_TELL_TEMPERATURE,  "how cold is it"),
    EN(VOICE_ACTION_TELL_HUMIDITY,     "what is the humidity"),
    EN(VOICE_ACTION_TELL_HUMIDITY,     "how humid is it"),

    /*
     * Sums that work with the Wi-Fi unplugged.
     *
     * MultiNet can only recognise phrases registered ahead of time, so these
     * are the exact sums the buddy can do with no internet at all. The answers
     * are NOT hard-coded — voice_math.c reads the phrase and works them out, so
     * adding a row here is all it takes to teach it another one.
     *
     * With Wi-Fi on, any sum works: the transcript goes through the same parser
     * before the question would have been sent to the language model.
     */
    EN(VOICE_ACTION_DO_MATH,           "what is two plus two"),
    EN(VOICE_ACTION_DO_MATH,           "what is five plus three"),
    EN(VOICE_ACTION_DO_MATH,           "what is nine plus seven"),
    EN(VOICE_ACTION_DO_MATH,           "what is ten minus four"),
    EN(VOICE_ACTION_DO_MATH,           "what is twenty minus six"),
    EN(VOICE_ACTION_DO_MATH,           "what is seven times six"),
    EN(VOICE_ACTION_DO_MATH,           "what is twelve times twelve"),
    EN(VOICE_ACTION_DO_MATH,           "what is twenty divided by four"),

    /* Music */
    EN(VOICE_ACTION_MUSIC_PLAY,        "play some music"),
    EN(VOICE_ACTION_MUSIC_PLAY,        "play me a song"),
    EN(VOICE_ACTION_MUSIC_PLAY,        "sing me a song"),
    EN(VOICE_ACTION_MUSIC_NEXT,        "play the next song"),
    EN(VOICE_ACTION_MUSIC_NEXT,        "skip this track"),
    EN(VOICE_ACTION_MUSIC_STOP,        "stop the music"),
    EN(VOICE_ACTION_MUSIC_STOP,        "be quiet please"),

    /* Expressions on demand */
    EN(VOICE_ACTION_EMOTE_HAPPY,       "show me a smile"),
    EN(VOICE_ACTION_EMOTE_HAPPY,       "be happy now"),
    EN(VOICE_ACTION_EMOTE_SAD,         "look very sad"),
    EN(VOICE_ACTION_EMOTE_SURPRISED,   "act surprised now"),
    EN(VOICE_ACTION_EMOTE_WINK,        "give me a wink"),
    EN(VOICE_ACTION_EMOTE_SILLY,       "do something silly"),
    EN(VOICE_ACTION_EMOTE_SILLY,       "make a funny face"),
    EN(VOICE_ACTION_EMOTE_EXCITED,     "get excited now"),
    EN(VOICE_ACTION_EMOTE_CURIOUS,     "look really curious"),
    EN(VOICE_ACTION_EMOTE_SLEEPY,      "act sleepy for me"),
    EN(VOICE_ACTION_EMOTE_LOVE,        "show me some love"),
    EN(VOICE_ACTION_EMOTE_CONFUSED,    "look confused now"),
    EN(VOICE_ACTION_EMOTE_DIZZY,       "make your eyes spin"),
    EN(VOICE_ACTION_EMOTE_YAWN,        "give me a big yawn"),
    EN(VOICE_ACTION_EMOTE_ANNOYED,     "pull a grumpy face"),
    EN(VOICE_ACTION_EMOTE_MISCHIEF,    "look a bit sneaky"),
    EN(VOICE_ACTION_EMOTE_PROUD,       "puff your chest out"),
    EN(VOICE_ACTION_EMOTE_BORED,       "pretend to be bored"),
    EN(VOICE_ACTION_EMOTE_NEUTRAL,     "go back to normal"),

    /* Expressions added in v2.9 */
    EN(VOICE_ACTION_EMOTE_SMUG,        "look pleased with yourself"),
    EN(VOICE_ACTION_EMOTE_SHOCKED,     "act totally shocked"),
    EN(VOICE_ACTION_EMOTE_DETERMINED,  "look determined now"),
    EN(VOICE_ACTION_EMOTE_GIGGLY,      "have a little giggle"),
    EN(VOICE_ACTION_EMOTE_SUSPICIOUS,  "look suspicious of me"),
    EN(VOICE_ACTION_EMOTE_RELIEVED,    "act relieved now"),
    EN(VOICE_ACTION_EMOTE_AMAZED,      "look totally amazed"),
    EN(VOICE_ACTION_EMOTE_NERVOUS,     "act a bit nervous"),

    /* Expressions added in v3.0 */
    EN(VOICE_ACTION_EMOTE_PLEADING,    "give me puppy eyes"),
    EN(VOICE_ACTION_EMOTE_TRIUMPHANT,  "celebrate with me"),
    EN(VOICE_ACTION_EMOTE_UNIMPRESSED, "look unimpressed"),
    EN(VOICE_ACTION_EMOTE_SCRUNCH,     "scrunch your face up"),
    EN(VOICE_ACTION_EMOTE_SHY,         "act all shy now"),
    EN(VOICE_ACTION_EMOTE_PANIC,       "start panicking now"),
    EN(VOICE_ACTION_EMOTE_DISGUSTED,   "look totally disgusted"),
    EN(VOICE_ACTION_EMOTE_SINGING,     "sing along with me"),

    /* Expressions added in v3.5 */
    EN(VOICE_ACTION_EMOTE_STARRY,      "look amazed at me"),
    EN(VOICE_ACTION_EMOTE_SCEPTICAL,   "give me a doubtful look"),
    EN(VOICE_ACTION_EMOTE_SHEEPISH,    "look a bit guilty"),
    EN(VOICE_ACTION_EMOTE_HOPEFUL,     "look hopeful now"),
    EN(VOICE_ACTION_EMOTE_THOUGHTFUL,  "have a little think"),
    EN(VOICE_ACTION_EMOTE_CHEEKY,      "be cheeky for me"),
    EN(VOICE_ACTION_EMOTE_ALARMED,     "look startled now"),
    EN(VOICE_ACTION_EMOTE_COSY,        "get comfortable"),
    EN(VOICE_ACTION_EMOTE_DREAMY,      "daydream a little"),
    EN(VOICE_ACTION_EMOTE_FOCUSED,     "concentrate very hard"),
    EN(VOICE_ACTION_EMOTE_PLAYFUL,     "be playful now"),
    EN(VOICE_ACTION_EMOTE_EXHAUSTED,   "look worn out"),

    /* Questions about itself, added in v3.5 */
    EN(VOICE_ACTION_HOW_LONG_AWAKE,    "how long have you been on"),
    EN(VOICE_ACTION_HOW_IS_YOUR_MEMORY,"how is your memory"),
    EN(VOICE_ACTION_NETWORK_STATUS,    "are you online"),
    EN(VOICE_ACTION_WHAT_IS_WAKE_WORD, "what is your wake word"),
    EN(VOICE_ACTION_GIVE_ADVICE,       "give me some advice"),
    EN(VOICE_ACTION_TELL_FORTUNE,      "read my fortune"),
    EN(VOICE_ACTION_TELL_STORY,        "tell me a story"),
    EN(VOICE_ACTION_ENCOURAGE_ME,      "cheer me up"),

    /*
     * The kitchen timer.
     *
     * MultiNet only ever matches phrases registered up front, so the durations
     * have to be spelled out one at a time rather than parsed from free speech.
     * These five cover almost every real use; voice_local_brain.c reads the
     * number back out of whichever phrase matched.
     */
    EN(VOICE_ACTION_TIMER_SET,         "set a timer for one minute"),
    EN(VOICE_ACTION_TIMER_SET,         "set a timer for two minutes"),
    EN(VOICE_ACTION_TIMER_SET,         "set a timer for five minutes"),
    EN(VOICE_ACTION_TIMER_SET,         "set a timer for ten minutes"),
    EN(VOICE_ACTION_TIMER_SET,         "set a timer for twenty minutes"),
    EN(VOICE_ACTION_TIMER_SET,         "set a timer for thirty minutes"),
    EN(VOICE_ACTION_TIMER_CHECK,       "how long is left"),
    EN(VOICE_ACTION_TIMER_CHECK,       "check my timer"),
    EN(VOICE_ACTION_TIMER_CANCEL,      "cancel the timer"),
    EN(VOICE_ACTION_TIMER_CANCEL,      "stop the timer"),

    /* Deciding things */
    EN(VOICE_ACTION_FLIP_COIN,         "flip a coin"),
    EN(VOICE_ACTION_FLIP_COIN,         "heads or tails"),
    EN(VOICE_ACTION_ROLL_DICE,         "roll a dice"),
    EN(VOICE_ACTION_ROLL_DICE,         "roll the dice for me"),
    EN(VOICE_ACTION_ROCK_PAPER_SCISSORS, "rock paper scissors"),
    EN(VOICE_ACTION_ROCK_PAPER_SCISSORS, "let us play a game"),
    EN(VOICE_ACTION_ORACLE,            "decide for me"),
    EN(VOICE_ACTION_ORACLE,            "should i do it"),

    /*
     * Conversation, added in v3.3 when the online brain became optional.
     *
     * A box nobody is paying to run still has to be worth talking to, and
     * "what can you do" answering with sixty ways to pull a face was a thin
     * offer. These are the questions people actually try first on something
     * with a face — its name, its age, whether it is a robot — plus the small
     * courtesies that make it feel like a companion rather than a remote
     * control.
     */
    EN(VOICE_ACTION_YOUR_NAME,         "what is your name"),
    EN(VOICE_ACTION_YOUR_NAME,         "who are you"),
    EN(VOICE_ACTION_HOW_OLD_ARE_YOU,   "how old are you"),
    EN(VOICE_ACTION_ARE_YOU_A_ROBOT,   "are you a robot"),
    EN(VOICE_ACTION_THANK_YOU,         "thank you buddy"),
    EN(VOICE_ACTION_COMPLIMENT,        "you are the best"),
    EN(VOICE_ACTION_COMPLIMENT,        "i really like you"),
    EN(VOICE_ACTION_GOOD_MORNING,      "good morning buddy"),
    EN(VOICE_ACTION_HIGH_FIVE,         "give me a high five"),
    EN(VOICE_ACTION_TELL_FACT,         "tell me a fact"),
    EN(VOICE_ACTION_TELL_FACT,         "teach me something"),
    EN(VOICE_ACTION_COUNT_TO_TEN,      "count to ten"),

    /* Device control */
    EN(VOICE_ACTION_VOLUME_UP,         "turn the volume up"),
    EN(VOICE_ACTION_VOLUME_DOWN,       "turn the volume down"),
    EN(VOICE_ACTION_OPEN_SETTINGS,     "open the settings"),
    EN(VOICE_ACTION_OPEN_CALCULATOR,   "open the calculator"),
    EN(VOICE_ACTION_OPEN_CALCULATOR,   "let me do a sum"),

    /* ---------------- Chinese -------------------------------------------- */

    CN(VOICE_ACTION_GREETING,          "你好小助手",   "ni hao xiao zhu shou"),
    CN(VOICE_ACTION_HOW_ARE_YOU,       "你今天好吗",   "ni jin tian hao ma"),
    CN(VOICE_ACTION_WHAT_CAN_YOU_DO,   "你会做什么",   "ni hui zuo shen me"),
    CN(VOICE_ACTION_TELL_JOKE,         "讲个笑话",     "jiang ge xiao hua"),
    CN(VOICE_ACTION_WHO_MADE_YOU,      "谁做的你",     "shui zuo de ni"),
    CN(VOICE_ACTION_GOODBYE,           "再见小助手",   "zai jian xiao zhu shou"),

    CN(VOICE_ACTION_TELL_TIME,         "现在几点了",   "xian zai ji dian le"),
    CN(VOICE_ACTION_TELL_DATE,         "今天几号",     "jin tian ji hao"),
    CN(VOICE_ACTION_TELL_TEMPERATURE,  "现在的温度",   "xian zai de wen du"),
    CN(VOICE_ACTION_TELL_HUMIDITY,     "现在的湿度",   "xian zai de shi du"),

    CN(VOICE_ACTION_MUSIC_PLAY,        "播放音乐",     "bo fang yin yue"),
    CN(VOICE_ACTION_MUSIC_NEXT,        "下一首歌",     "xia yi shou ge"),
    CN(VOICE_ACTION_MUSIC_STOP,        "停止播放",     "ting zhi bo fang"),

    CN(VOICE_ACTION_EMOTE_HAPPY,       "笑一个吧",     "xiao yi ge ba"),
    CN(VOICE_ACTION_EMOTE_SAD,         "做个难过的表情", "zuo ge nan guo de biao qing"),
    CN(VOICE_ACTION_EMOTE_SURPRISED,   "做个惊讶的表情", "zuo ge jing ya de biao qing"),
    CN(VOICE_ACTION_EMOTE_WINK,        "眨一下眼睛",   "zha yi xia yan jing"),
    CN(VOICE_ACTION_EMOTE_SILLY,       "做个鬼脸",     "zuo ge gui lian"),

    CN(VOICE_ACTION_VOLUME_UP,         "音量大一点",   "yin liang da yi dian"),
    CN(VOICE_ACTION_VOLUME_DOWN,       "音量小一点",   "yin liang xiao yi dian"),
    CN(VOICE_ACTION_OPEN_SETTINGS,     "打开设置",     "da kai she zhi"),
};

uint16_t voice_commands_get_count(void)
{
    return (uint16_t)(sizeof(s_commands) / sizeof(s_commands[0]));
}

const voice_command_t *voice_commands_get(uint16_t index)
{
    if (index >= voice_commands_get_count()) {
        return NULL;
    }
    return &s_commands[index];
}

const char *voice_commands_action_title(voice_action_t action)
{
    switch (action) {
    case VOICE_ACTION_GREETING:         return "Say hello";
    case VOICE_ACTION_HOW_ARE_YOU:      return "Ask how it is";
    case VOICE_ACTION_WHAT_CAN_YOU_DO:  return "Ask what it can do";
    case VOICE_ACTION_TELL_JOKE:        return "Ask for a joke";
    case VOICE_ACTION_WHO_MADE_YOU:     return "Ask who made it";
    case VOICE_ACTION_GOODBYE:          return "Send it to sleep";

    case VOICE_ACTION_TELL_TIME:        return "Ask the time";
    case VOICE_ACTION_TELL_DATE:        return "Ask the date";
    case VOICE_ACTION_TELL_TEMPERATURE: return "Ask the temperature";
    case VOICE_ACTION_TELL_HUMIDITY:    return "Ask the humidity";
    case VOICE_ACTION_DO_MATH:          return "Ask it to do a sum";

    case VOICE_ACTION_MUSIC_PLAY:       return "Play music";
    case VOICE_ACTION_MUSIC_NEXT:       return "Skip a track";
    case VOICE_ACTION_MUSIC_STOP:       return "Stop the music";

    case VOICE_ACTION_EMOTE_HAPPY:      return "Make it smile";
    case VOICE_ACTION_EMOTE_SAD:        return "Make it sad";
    case VOICE_ACTION_EMOTE_SURPRISED:  return "Make it surprised";
    case VOICE_ACTION_EMOTE_WINK:       return "Make it wink";
    case VOICE_ACTION_EMOTE_SILLY:      return "Make it act silly";
    case VOICE_ACTION_EMOTE_EXCITED:    return "Make it excited";
    case VOICE_ACTION_EMOTE_CURIOUS:    return "Make it curious";
    case VOICE_ACTION_EMOTE_SLEEPY:     return "Make it sleepy";
    case VOICE_ACTION_EMOTE_LOVE:       return "Make it smitten";
    case VOICE_ACTION_EMOTE_CONFUSED:   return "Make it confused";
    case VOICE_ACTION_EMOTE_DIZZY:      return "Make it dizzy";
    case VOICE_ACTION_EMOTE_YAWN:       return "Make it yawn";
    case VOICE_ACTION_EMOTE_ANNOYED:    return "Make it grumpy";
    case VOICE_ACTION_EMOTE_MISCHIEF:   return "Make it sneaky";
    case VOICE_ACTION_EMOTE_PROUD:      return "Make it proud";
    case VOICE_ACTION_EMOTE_BORED:      return "Make it bored";
    case VOICE_ACTION_EMOTE_SMUG:       return "Make it smug";
    case VOICE_ACTION_EMOTE_SHOCKED:    return "Make it shocked";
    case VOICE_ACTION_EMOTE_DETERMINED: return "Make it determined";
    case VOICE_ACTION_EMOTE_GIGGLY:     return "Make it giggle";
    case VOICE_ACTION_EMOTE_SUSPICIOUS: return "Make it suspicious";
    case VOICE_ACTION_EMOTE_RELIEVED:   return "Make it relieved";
    case VOICE_ACTION_EMOTE_AMAZED:     return "Make it amazed";
    case VOICE_ACTION_EMOTE_NERVOUS:    return "Make it nervous";
    case VOICE_ACTION_EMOTE_PLEADING:   return "Make it beg";
    case VOICE_ACTION_EMOTE_TRIUMPHANT: return "Make it celebrate";
    case VOICE_ACTION_EMOTE_UNIMPRESSED:return "Make it unimpressed";
    case VOICE_ACTION_EMOTE_SCRUNCH:    return "Make it scrunch up";
    case VOICE_ACTION_EMOTE_SHY:        return "Make it shy";
    case VOICE_ACTION_EMOTE_PANIC:      return "Make it panic";
    case VOICE_ACTION_EMOTE_DISGUSTED:  return "Make it disgusted";
    case VOICE_ACTION_EMOTE_SINGING:    return "Make it sing";
    case VOICE_ACTION_EMOTE_STARRY:     return "Make it starry-eyed";
    case VOICE_ACTION_EMOTE_SCEPTICAL:  return "Make it doubt you";
    case VOICE_ACTION_EMOTE_SHEEPISH:   return "Make it sheepish";
    case VOICE_ACTION_EMOTE_HOPEFUL:    return "Make it hopeful";
    case VOICE_ACTION_EMOTE_THOUGHTFUL: return "Make it think";
    case VOICE_ACTION_EMOTE_CHEEKY:     return "Make it cheeky";
    case VOICE_ACTION_EMOTE_ALARMED:    return "Make it startled";
    case VOICE_ACTION_EMOTE_COSY:       return "Make it cosy";
    case VOICE_ACTION_EMOTE_DREAMY:     return "Make it daydream";
    case VOICE_ACTION_EMOTE_FOCUSED:    return "Make it concentrate";
    case VOICE_ACTION_EMOTE_PLAYFUL:    return "Make it playful";
    case VOICE_ACTION_EMOTE_EXHAUSTED:  return "Make it worn out";
    case VOICE_ACTION_EMOTE_NEUTRAL:    return "Back to its normal face";

    case VOICE_ACTION_HOW_LONG_AWAKE:   return "Ask how long it has been on";
    case VOICE_ACTION_HOW_IS_YOUR_MEMORY: return "Ask about its memory";
    case VOICE_ACTION_NETWORK_STATUS:   return "Ask if it is online";
    case VOICE_ACTION_WHAT_IS_WAKE_WORD:return "Ask for the wake word";
    case VOICE_ACTION_GIVE_ADVICE:      return "Ask for advice";
    case VOICE_ACTION_TELL_FORTUNE:     return "Ask for a fortune";
    case VOICE_ACTION_TELL_STORY:       return "Ask for a story";
    case VOICE_ACTION_ENCOURAGE_ME:     return "Ask to be cheered up";

    case VOICE_ACTION_TIMER_SET:        return "Set a timer";
    case VOICE_ACTION_TIMER_CHECK:      return "Check the timer";
    case VOICE_ACTION_TIMER_CANCEL:     return "Cancel the timer";

    case VOICE_ACTION_FLIP_COIN:        return "Flip a coin";
    case VOICE_ACTION_ROLL_DICE:        return "Roll a dice";
    case VOICE_ACTION_ROCK_PAPER_SCISSORS: return "Play rock paper scissors";
    case VOICE_ACTION_ORACLE:           return "Let it decide";

    case VOICE_ACTION_YOUR_NAME:        return "Ask its name";
    case VOICE_ACTION_HOW_OLD_ARE_YOU:  return "Ask its age";
    case VOICE_ACTION_ARE_YOU_A_ROBOT:  return "Ask if it is a robot";
    case VOICE_ACTION_THANK_YOU:        return "Thank it";
    case VOICE_ACTION_COMPLIMENT:       return "Pay it a compliment";
    case VOICE_ACTION_GOOD_MORNING:     return "Say good morning";
    case VOICE_ACTION_HIGH_FIVE:        return "High five";
    case VOICE_ACTION_TELL_FACT:        return "Ask for a fact";
    case VOICE_ACTION_COUNT_TO_TEN:     return "Ask it to count";

    case VOICE_ACTION_VOLUME_UP:        return "Turn it up";
    case VOICE_ACTION_VOLUME_DOWN:      return "Turn it down";
    case VOICE_ACTION_OPEN_SETTINGS:    return "Open settings";
    case VOICE_ACTION_OPEN_CALCULATOR:  return "Open the calculator";

    case VOICE_ACTION_COUNT:
    case VOICE_ACTION_NONE:
    default:                            return "Other";
    }
}

/**
 * Actions worth advertising when the user asks "what can you do". One example
 * phrase each — enough to teach the pattern without reciting the whole table.
 */
static const voice_action_t s_headline_actions[] = {
    VOICE_ACTION_TELL_TIME,
    VOICE_ACTION_TELL_TEMPERATURE,
    VOICE_ACTION_MUSIC_PLAY,
    VOICE_ACTION_TELL_JOKE,
    VOICE_ACTION_TELL_FACT,
    VOICE_ACTION_EMOTE_SILLY,
};

void voice_commands_describe(buddy_language_t language, char *buffer, size_t buffer_size)
{
    if (buffer == NULL || buffer_size == 0) {
        return;
    }
    buffer[0] = '\0';

    size_t used = 0;
    const size_t headline_count = sizeof(s_headline_actions) / sizeof(s_headline_actions[0]);

    for (size_t h = 0; h < headline_count; h++) {
        /* Find the first phrase in the requested language for this action. */
        for (uint16_t i = 0; i < voice_commands_get_count(); i++) {
            const voice_command_t *command = &s_commands[i];
            if (command->language != language || command->action != s_headline_actions[h]) {
                continue;
            }

            const int written = snprintf(buffer + used, buffer_size - used,
                                         "%s\"%s\"", (used > 0) ? ", " : "", command->phrase);
            if (written < 0 || (size_t)written >= buffer_size - used) {
                return;   /* Ran out of room — what we have is still readable. */
            }
            used += (size_t)written;
            break;
        }
    }
}
