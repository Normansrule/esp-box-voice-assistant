/*
 * voice_local_brain.c — The offline answer bank.
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */

#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"

#include "audio_music.h"
#include "buddy_settings.h"
#include "buddy_timer.h"
#include "hw_sensors.h"
#include "hw_speaker.h"
#include "net_wifi.h"
#include "voice_commands.h"
#include "voice_local_brain.h"
#include "voice_math.h"
#include "voice_number_words.h"
#include "voice_speech_parts.h"

static const char *TAG = "local_brain";

/** Volume moves in steps this big when the user asks for louder or quieter. */
#define VOLUME_STEP_PERCENT     10

/* ------------------------------------------------------------------------ */
/* Canned lines                                                              */
/* ------------------------------------------------------------------------ */

/*
 * Several options per answer so the buddy does not sound like a recording.
 * Add your own freely — they are picked at random.
 */

static const char *const s_greetings[] = {
    "Hey! Good to see you.",
    "Hello there. I was hoping you would say something.",
    "Hi! What are we up to today?",
    "Oh hey, you are back.",
};

static const char *const s_how_are_you[] = {
    "Warm, well powered, and pleased about it.",
    "Excellent. My eyes work and everything.",
    "Pretty good for a small box on a desk.",
    "Happy. Slightly bored. Mostly happy.",
};

static const char *const s_jokes[] = {
    "Why did the microcontroller go to therapy? Too many unresolved interrupts.",
    "I would tell you a joke about UDP, but you might not get it.",
    "There are ten kinds of people. Those who read binary, and those who do not.",
    "My favourite kind of music? Anything with a good clock signal.",
    "I asked the capacitor for advice. It said give it time, it will come around.",
    "Why was the robot tired? It had a hard drive.",
    "I would go outside, but the cable is only so long.",
    "I told a joke about flash memory once. Nobody remembers it.",
    "My therapist says I have too many unhandled exceptions. I said, throw me another one.",
    "What do you call a robot that takes the long way round? R2 detour.",
    "I tried to catch fog yesterday. Mist.",
    "Two antennas got married. The wedding was terrible, but the reception was excellent.",
    "Why do programmers prefer dark mode? Because light attracts bugs.",
    "I would make a joke about the wake word, but you would have to say it first.",
    "How does a robot eat? In megabites.",
};

static const char *const s_who_made_you[] = {
    "I run on an ESP32-S3 box, and somebody at this desk taught me to make faces.",
    "Espressif built the hardware. The personality came later.",
};

static const char *const s_goodbyes[] = {
    "Night night. Poke me if you need anything.",
    "Alright, resting my eyes. Say the wake word whenever.",
    "See you soon.",
};

/*
 * The conversational answers added in v3.3.
 *
 * Every line is a WHOLE FIXED SENTENCE, and that is not a style choice. The
 * offline voice works by hashing the reply text and looking for a matching mp3
 * on flash, so a sentence assembled at runtime has nothing to look up and stays
 * silent. Written out in full, each of these gets recorded by
 * scripts/make_voice_clips.py and spoken aloud with no network at all.
 *
 * Reword any of them and you must re-run that script, or that one line goes
 * quiet — which is the failure the hash is designed to cause, rather than
 * letting the buddy confidently say the wrong sentence.
 */

static const char *const s_names[] = {
    "I have not been given one. You could pick something.",
    "No name yet. I answer to hi E S P, which is not the same thing.",
    "Whatever you call me, as long as you start with the wake word.",
};

static const char *const s_ages[] = {
    "I have been awake since you last plugged me in. Before that, nothing.",
    "Older than this conversation, younger than the desk.",
    "I lose track every time the power goes.",
};

static const char *const s_robot_answers[] = {
    "A small one, on a desk, with opinions about the temperature.",
    "Technically I am a development board with a face. But yes.",
    "I am about as robotic as a thing without arms can be.",
};

static const char *const s_thanks_answers[] = {
    "Any time. That is the whole job.",
    "You are very welcome.",
    "Happy to help. It beats sitting here blinking.",
};

static const char *const s_compliment_answers[] = {
    "Oh. Well now I am going to be insufferable.",
    "That is going straight into my permanent memory. Which is small, so it counts.",
    "You are alright yourself.",
};

static const char *const s_good_mornings[] = {
    "Morning! I have been up for a while.",
    "Good morning. The desk missed you.",
    "Morning. I checked, and the temperature is fine.",
};

static const char *const s_high_fives[] = {
    "I have no hands, but consider it high fived.",
    "Up high! Imagine the sound.",
    "That was a good one. I felt it.",
};

/*
 * Facts chosen to be true, short, and about the things sitting on this desk —
 * the chip, the sensors, sound, and time. A box that knows something about
 * itself is more interesting than one reciting trivia.
 */
static const char *const s_facts[] = {
    "The chip inside me has two processor cores, and I use both. One listens while the other draws my face.",
    "My wake word detector never stops running. It checks the microphone about thirty times a second, all day.",
    "Sound is just air pressure wobbling. My speaker wobbles it, your eardrum notices, and that is the whole trick.",
    "I record you at sixteen thousand samples a second. That is the rate speech recognition was tuned for.",
    "Humidity is relative. Warm air holds more water, so the same air gets drier just by being heated.",
    "My display is three hundred and twenty pixels across. That is fewer pixels than one letter on a modern phone.",
    "A quartz clock crystal is cut so precisely that it vibrates the same number of times every second, forever.",
    "The red I am drawn in is Espressif's. I did not choose it, but I have grown fond of it.",
    "Flash memory forgets slowly. Left unpowered for twenty years, I might lose track of who I am.",
    "There is no battery in me. Every time you unplug me, I forget what time it is and have to ask the internet.",
};

/*
 * Rock paper scissors and the yes-no oracle. Fixed sentences again, one per
 * possible outcome, so the buddy can say the result out loud rather than only
 * showing it.
 */
static const char *const s_rock_paper_scissors[] = {
    "Rock.",
    "Paper.",
    "Scissors.",
};

static const char *const s_oracle_answers[] = {
    "Yes. Do it.",
    "No. Leave it.",
    "Yes, but not today.",
    "I would not, and I think you already knew that.",
    "Definitely. I am very confident about this.",
    "Ask me again in a minute, I am not sure.",
    "The answer is no, and you are allowed to ignore me.",
    "Go on then.",
};

static const char *const s_counting[] = {
    "One. Two. Three. Four. Five. Six. Seven. Eight. Nine. Ten. There.",
};

static const char *const s_wake_word_answers[] = {
    "Say hi E S P, wait for the chirp, then talk. That is the whole trick.",
    "Hi E S P. Then wait for the little chirp before you say the rest.",
};

/*
 * Advice, fortunes and encouragement.
 *
 * Written to be useful rather than mystical. A desk toy that tells you to
 * drink some water is a better desk toy than one that tells you a stranger
 * will bring news, and it costs exactly the same to store.
 */
static const char *const s_advice[] = {
    "Do the small annoying one first. The big one gets easier once it is alone.",
    "If you have read the same line three times, the problem is not the line.",
    "Write down what you are about to try. Half the time you will spot it while writing.",
    "Stand up. That is the advice. You have been sitting for ages.",
    "When it works and you do not know why, find out now, not later.",
    "Drink some water. I can see the humidity in here and you are losing.",
    "Name it after what it does, not after what it is. Future you will thank present you.",
    "If you are stuck, explain it out loud to me. I am told that works even on a box.",
};

static const char *const s_fortunes[] = {
    "Something you have been avoiding will take eleven minutes.",
    "A thing you built will be used by someone who never thinks about you. That is the good ending.",
    "You will find the bug in the last place you would have looked, which is where it always was.",
    "Today you will be right about something and it will not matter. Enjoy it anyway.",
    "A small kindness this afternoon. Possibly from me.",
    "The thing you are dreading is a five minute job with a scary name.",
};

static const char *const s_encouragement[] = {
    "You are doing better than you think. I have been watching, and I would know.",
    "That was hard and you did it anyway. That is the whole skill.",
    "Nobody else could tell how much of that was guesswork. It counts the same.",
    "I have a face and no hands, so trust me when I say you are ahead.",
    "Whatever it is, you have solved worse. I was there for some of it.",
};

static const char *const s_stories[] = {
    "Once there was a box on a desk. It could not move, so it learned to make faces. "
    "Eventually somebody looked up, and that was enough. The end.",

    "A capacitor and a resistor walked into a circuit. The capacitor said, give it time. "
    "The resistor said, I am holding everything back. They were both right. The end.",

    "There was a clock that had never seen the sun. It asked the network what time it was, "
    "every morning, and believed the answer. It was never once wrong. The end.",
};

/**
 * @brief Pick one line at random from a table of alternatives.
 */
static const char *pick_random(const char *const *options, size_t option_count)
{
    if (options == NULL || option_count == 0) {
        return "";
    }
    return options[esp_random() % option_count];
}

/** Shorthand so the call sites below stay short. */
#define PICK(table) pick_random(table, sizeof(table) / sizeof((table)[0]))

/* ------------------------------------------------------------------------ */
/* Helpers that build an answer                                              */
/* ------------------------------------------------------------------------ */

/**
 * @brief Start a reply from a clean slate.
 */
static void reply_reset(voice_reply_t *reply)
{
    memset(reply, 0, sizeof(*reply));
    reply->emotion = FACE_EMOTION_HAPPY;
    reply->speak_aloud = true;
}

/**
 * @brief Fill in the reply text, safely truncating anything over-long.
 */
static void reply_set_text(voice_reply_t *reply, const char *format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    vsnprintf(reply->text, sizeof(reply->text), format, arguments);
    va_end(arguments);
}

/**
 * @brief True once the clock has been set from the network.
 *
 * Before the first time sync the clock sits in 1970, so any year before 2023 is
 * a reliable sign that the buddy does not actually know what time it is.
 */
static bool clock_is_trustworthy(const struct tm *now)
{
    return (now->tm_year + 1900) >= 2023;
}

/* ------------------------------------------------------------------------ */
/* The individual answers                                                    */
/* ------------------------------------------------------------------------ */

/**
 * @brief Which part of the day it is, as the fragment that names it.
 */
static const char *part_of_day(int hour_24)
{
    if (hour_24 < 12) {
        return VOICE_FRAGMENT_MORNING;
    }
    if (hour_24 < 18) {
        return VOICE_FRAGMENT_AFTERNOON;
    }
    return VOICE_FRAGMENT_EVENING;
}

static void answer_time(voice_reply_t *reply)
{
    time_t now_seconds = time(NULL);
    struct tm now;
    localtime_r(&now_seconds, &now);

    if (!clock_is_trustworthy(&now)) {
        reply->emotion = FACE_EMOTION_CONFUSED;
        reply_set_text(reply,
                       "I have lost track of the time. Connect me to Wi-Fi in "
                       "Settings and I will find a clock.");
        return;
    }

    /* Twelve-hour clock, because that is how people ask. */
    int hour = now.tm_hour % 12;
    if (hour == 0) {
        hour = 12;
    }

    reply->emotion = FACE_EMOTION_NEUTRAL;
    reply_set_text(reply, "It is %d:%02d %s.", hour, now.tm_min,
                   (now.tm_hour < 12) ? "in the morning"
                                      : (now.tm_hour < 18) ? "in the afternoon"
                                                           : "in the evening");

    /*
     * And how to say it. "It is / three / forty seven / in the afternoon" —
     * four recordings that between them cover every minute of the day, which no
     * amount of flash could hold as whole sentences.
     *
     * The minutes need three shapes to sound right: on the hour it is
     * "o'clock", under ten past it needs the "oh" that a person says out loud
     * and never writes down, and after that it is just the number.
     */
    voice_speech_plan_t *plan = &reply->speech;

    voice_speech_plan_add(plan, VOICE_FRAGMENT_IT_IS);
    voice_speech_plan_add_number(plan, hour);

    if (now.tm_min == 0) {
        voice_speech_plan_add(plan, VOICE_FRAGMENT_OCLOCK);
    } else if (now.tm_min < 10) {
        voice_speech_plan_add(plan, VOICE_FRAGMENT_OH);
        voice_speech_plan_add_number(plan, now.tm_min);
    } else {
        voice_speech_plan_add_number(plan, now.tm_min);
    }

    voice_speech_plan_add(plan, part_of_day(now.tm_hour));
}

static void answer_date(voice_reply_t *reply)
{
    time_t now_seconds = time(NULL);
    struct tm now;
    localtime_r(&now_seconds, &now);

    if (!clock_is_trustworthy(&now)) {
        reply->emotion = FACE_EMOTION_CONFUSED;
        reply_set_text(reply, "No idea. My calendar needs Wi-Fi.");
        return;
    }

    char formatted[64];
    strftime(formatted, sizeof(formatted), "%A, %B %d", &now);

    reply->emotion = FACE_EMOTION_NEUTRAL;
    reply_set_text(reply, "Today is %s.", formatted);

    /* "Today is / Wednesday / September / the / nineteen" */
    voice_speech_plan_t *plan = &reply->speech;

    voice_speech_plan_add(plan, VOICE_FRAGMENT_TODAY_IS);
    voice_speech_plan_add(plan, voice_speech_weekday(now.tm_wday));
    voice_speech_plan_add(plan, voice_speech_month(now.tm_mon));
    voice_speech_plan_add(plan, VOICE_FRAGMENT_THE);
    voice_speech_plan_add_number(plan, now.tm_mday);
}

static void answer_temperature(voice_reply_t *reply)
{
    const hw_sensor_reading_t reading = hw_sensors_read_environment();

    if (!reading.valid) {
        reply->emotion = FACE_EMOTION_SAD;
        reply_set_text(reply,
                       "I cannot feel the room. My sensor dock is not attached.");
        return;
    }

    const buddy_settings_t *settings = buddy_settings_get();
    const bool fahrenheit_wanted = (settings->temperature_unit == BUDDY_TEMPERATURE_FAHRENHEIT);

    const float shown = fahrenheit_wanted
                        ? hw_sensors_celsius_to_fahrenheit(reading.temperature_celsius)
                        : reading.temperature_celsius;

    reply_set_text(reply, "It is %.1f degrees %s in here.",
                   shown, fahrenheit_wanted ? "Fahrenheit" : "Celsius");

    /*
     * Spoken to the nearest whole degree, while the screen keeps the decimal.
     *
     * A recording per tenth of a degree would be a thousand clips to say
     * something nobody asks for; "twenty two degrees" is how a person answers
     * "how warm is it", and the exact figure is right there in the bubble for
     * anyone who wants it.
     */
    voice_speech_plan_t *plan = &reply->speech;
    const int whole = (int)((shown < 0.0f) ? (shown - 0.5f) : (shown + 0.5f));

    voice_speech_plan_add(plan, VOICE_FRAGMENT_IT_IS);
    if (whole < 0) {
        voice_speech_plan_add(plan, VOICE_FRAGMENT_MINUS);
    }
    voice_speech_plan_add_number(plan, (whole < 0) ? -whole : whole);
    voice_speech_plan_add(plan, fahrenheit_wanted ? VOICE_FRAGMENT_DEGREES_F
                                                  : VOICE_FRAGMENT_DEGREES_C);
    voice_speech_plan_add(plan, VOICE_FRAGMENT_IN_HERE);

    /* React to the reading — a bit of opinion makes it feel like a companion. */
    if (reading.temperature_celsius > 27.0f) {
        reply->emotion = FACE_EMOTION_ANNOYED;
    } else if (reading.temperature_celsius < 17.0f) {
        reply->emotion = FACE_EMOTION_SAD;
    } else {
        reply->emotion = FACE_EMOTION_HAPPY;
    }
}

static void answer_humidity(voice_reply_t *reply)
{
    const hw_sensor_reading_t reading = hw_sensors_read_environment();

    if (!reading.valid) {
        reply->emotion = FACE_EMOTION_SAD;
        reply_set_text(reply, "No sensor dock, so no humidity reading.");
        return;
    }

    reply_set_text(reply, "Humidity is %.0f percent.", reading.humidity_percent);

    voice_speech_plan_add(&reply->speech, VOICE_FRAGMENT_HUMIDITY_IS);
    voice_speech_plan_add_number(&reply->speech, (int)(reading.humidity_percent + 0.5f));
    voice_speech_plan_add(&reply->speech, VOICE_FRAGMENT_PERCENT);

    if (reading.humidity_percent > 65.0f) {
        reply->emotion = FACE_EMOTION_ANNOYED;
    } else if (reading.humidity_percent < 25.0f) {
        reply->emotion = FACE_EMOTION_CONFUSED;
    } else {
        reply->emotion = FACE_EMOTION_HAPPY;
    }
}

static void answer_capabilities(voice_reply_t *reply)
{
    char examples[240];
    voice_commands_describe(buddy_settings_get()->language, examples, sizeof(examples));

    reply->emotion = FACE_EMOTION_EXCITED;

    /*
     * Speaking two examples is a poor answer to "what can you do" when there
     * are sixty phrases. Say a couple out loud for the sake of a natural reply,
     * and put the whole list on screen at the same time.
     */
    reply->open_examples = true;

    if (buddy_settings_has_cloud_credentials()) {
        reply_set_text(reply,
                       "Here is my whole list. Try %s — or just ask me anything, "
                       "since I am connected.", examples);
    } else {
        /*
         * No sales pitch for the online brain here.
         *
         * This used to end "Add an API key in Settings and I can answer open
         * questions too", which turns the answer to "what can you do" into an
         * advert for a paid extra. The honest answer is the hundred-odd things
         * it does for nothing, and those are already on screen.
         */
        reply_set_text(reply, "Here is my whole list. Try %s.", examples);
    }
}

static void handle_music_play(voice_reply_t *reply)
{
    char track_name[64] = {0};

    if (audio_music_play_random(track_name, sizeof(track_name)) == ESP_OK) {
        reply->emotion = FACE_EMOTION_SINGING;
        reply->speak_aloud = false;   /* Do not talk over the song we just started. */
        reply_set_text(reply, "Playing %s", track_name);
        return;
    }

    reply->emotion = FACE_EMOTION_SAD;
    reply_set_text(reply,
                   "I have no music to play. Drop some .mp3 files into the "
                   "spiffs music folder and reflash, or set a stream address "
                   "in Settings.");
}

static void handle_music_next(voice_reply_t *reply)
{
    char track_name[64] = {0};

    if (audio_music_play_next(track_name, sizeof(track_name)) == ESP_OK) {
        reply->emotion = FACE_EMOTION_SINGING;
        reply->speak_aloud = false;
        reply_set_text(reply, "Next up: %s", track_name);
        return;
    }

    reply->emotion = FACE_EMOTION_CONFUSED;
    reply_set_text(reply, "There is nothing else in the playlist.");
}

/* ------------------------------------------------------------------------ */
/* Questions about itself                                                    */
/* ------------------------------------------------------------------------ */

static void answer_uptime(voice_reply_t *reply)
{
    const int64_t up_us = esp_timer_get_time();
    const uint32_t total_minutes = (uint32_t)(up_us / (60LL * 1000000LL));
    const uint32_t hours = total_minutes / 60u;
    const uint32_t minutes = total_minutes % 60u;

    reply->emotion = (hours >= 8u) ? FACE_EMOTION_EXHAUSTED : FACE_EMOTION_NEUTRAL;

    if (hours == 0u) {
        reply_set_text(reply, "I have been awake for %lu minute%s.",
                       (unsigned long)minutes, (minutes == 1u) ? "" : "s");

        voice_speech_plan_add(&reply->speech, VOICE_FRAGMENT_AWAKE_FOR);
        voice_speech_plan_add_number(&reply->speech, (int)minutes);
        voice_speech_plan_add(&reply->speech, (minutes == 1u) ? VOICE_FRAGMENT_MINUTE
                                                              : VOICE_FRAGMENT_MINUTES);
        return;
    }

    reply_set_text(reply, "I have been awake for %lu hour%s and %lu minute%s.",
                   (unsigned long)hours, (hours == 1u) ? "" : "s",
                   (unsigned long)minutes, (minutes == 1u) ? "" : "s");

    voice_speech_plan_add(&reply->speech, VOICE_FRAGMENT_AWAKE_FOR);
    voice_speech_plan_add_number(&reply->speech, (int)hours);
    voice_speech_plan_add(&reply->speech, (hours == 1u) ? VOICE_FRAGMENT_HOUR
                                                        : VOICE_FRAGMENT_HOURS);
    voice_speech_plan_add(&reply->speech, VOICE_FRAGMENT_AND);
    voice_speech_plan_add_number(&reply->speech, (int)minutes);
    voice_speech_plan_add(&reply->speech, (minutes == 1u) ? VOICE_FRAGMENT_MINUTE
                                                          : VOICE_FRAGMENT_MINUTES);
}

static void answer_memory(voice_reply_t *reply)
{
    const size_t internal_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    const int kilobytes = (int)(internal_free / 1024u);

    /*
     * Internal RAM, not PSRAM, because internal is the one that runs out — see
     * the long note in buddy_selftest.c. There are megabytes of PSRAM and it
     * has never been the thing that broke.
     */
    reply->emotion = (kilobytes < 45) ? FACE_EMOTION_NERVOUS : FACE_EMOTION_PROUD;
    reply_set_text(reply, "I have %d kilobytes of fast memory free.", kilobytes);

    voice_speech_plan_add(&reply->speech, VOICE_FRAGMENT_MEMORY_FREE);
    voice_speech_plan_add_number(&reply->speech, kilobytes);
    voice_speech_plan_add(&reply->speech, VOICE_FRAGMENT_KB_INTERNAL);
}

static void answer_network(voice_reply_t *reply)
{
    if (!net_wifi_is_connected()) {
        reply->emotion = FACE_EMOTION_SHEEPISH;
        reply_set_text(reply, "I am not on Wi-Fi at the moment.");
        return;
    }

    const char *address = net_wifi_get_ip_address();

    /*
     * The last group of the address, not the whole thing. "One nine two dot one
     * six eight dot one dot two one two" is a mouthful nobody wants read aloud,
     * and the only part that differs between boxes on the same network is the
     * end — which is exactly the part someone asking this needs.
     */
    const char *last_group = address;
    for (const char *c = address; *c != '\0'; c++) {
        if (*c == '.') {
            last_group = c + 1;
        }
    }

    reply->emotion = FACE_EMOTION_FOCUSED;
    reply_set_text(reply, "I am online as %s on %s.", address, net_wifi_get_ssid());

    voice_speech_plan_add(&reply->speech, VOICE_FRAGMENT_ONLINE_AS);
    voice_speech_plan_add_number(&reply->speech, atoi(last_group));
}

static void handle_volume(voice_reply_t *reply, int direction)
{
    buddy_settings_t *settings = buddy_settings_get();

    int volume = (int)settings->speaker_volume + (direction * VOLUME_STEP_PERCENT);
    if (volume > 100) {
        volume = 100;
    }
    if (volume < 0) {
        volume = 0;
    }

    settings->speaker_volume = (uint8_t)volume;
    hw_speaker_set_volume(settings->speaker_volume);
    buddy_settings_save();

    reply->emotion = (direction > 0) ? FACE_EMOTION_EXCITED : FACE_EMOTION_NEUTRAL;
    reply_set_text(reply, "Volume %s to %d percent.",
                   (direction > 0) ? "up" : "down", volume);

    voice_speech_plan_add(&reply->speech, (direction > 0) ? VOICE_FRAGMENT_VOLUME_UP_TO
                                                          : VOICE_FRAGMENT_VOLUME_DOWN_TO);
    voice_speech_plan_add_number(&reply->speech, volume);
    voice_speech_plan_add(&reply->speech, VOICE_FRAGMENT_PERCENT);
}

/* ------------------------------------------------------------------------ */
/* Public API                                                                */
/* ------------------------------------------------------------------------ */

void voice_local_brain_init(void)
{
    ESP_LOGI(TAG, "Offline answers ready");
}

/* ------------------------------------------------------------------------ */
/* Expressions on demand                                                     */
/* ------------------------------------------------------------------------ */

/**
 * Every expression the user can ask for by name.
 *
 * A table rather than a switch, because "add another emote" should be a
 * one-line change. To add one: add the expression to face_emotion_t and its
 * pose to face_emotions.c, add an action to voice_action_t, add a phrase and a
 * title to voice_commands.c, then add a row here. The host tests will tell you
 * if you miss any of those steps.
 */
typedef struct {
    voice_action_t action;
    face_emotion_t emotion;
    const char    *line;        /*!< What it says while pulling the face */
    bool           play_antic;  /*!< Follow it with a little performance */
} emote_request_t;

static const emote_request_t s_emote_requests[] = {
    { VOICE_ACTION_EMOTE_HAPPY,     FACE_EMOTION_HAPPY,     "There you go!",                 false },
    { VOICE_ACTION_EMOTE_SAD,       FACE_EMOTION_SAD,       "Okay, but only for a moment.",  false },
    { VOICE_ACTION_EMOTE_SURPRISED, FACE_EMOTION_SURPRISED, "Oh! Goodness.",                 false },
    { VOICE_ACTION_EMOTE_WINK,      FACE_EMOTION_WINK,      "Any time.",                     false },
    { VOICE_ACTION_EMOTE_SILLY,     FACE_EMOTION_DIZZY,     "Watch this.",                   true  },
    { VOICE_ACTION_EMOTE_EXCITED,   FACE_EMOTION_EXCITED,   "Yes! What are we doing?",       false },
    { VOICE_ACTION_EMOTE_CURIOUS,   FACE_EMOTION_CURIOUS,   "Hmm. Go on then.",              false },
    { VOICE_ACTION_EMOTE_SLEEPY,    FACE_EMOTION_SLEEPY,    "I was already halfway there.",  false },
    { VOICE_ACTION_EMOTE_LOVE,      FACE_EMOTION_LOVE,      "Oh, stop it. Carry on.",        false },
    { VOICE_ACTION_EMOTE_CONFUSED,  FACE_EMOTION_CONFUSED,  "Wait, what?",                   false },
    { VOICE_ACTION_EMOTE_DIZZY,     FACE_EMOTION_DIZZY,     "Whoa. Everything is spinning.", false },
    { VOICE_ACTION_EMOTE_YAWN,      FACE_EMOTION_YAWN,      "Do not blame me, you asked.",   false },
    { VOICE_ACTION_EMOTE_ANNOYED,   FACE_EMOTION_ANNOYED,   "Fine. Grumpy it is.",           false },
    { VOICE_ACTION_EMOTE_MISCHIEF,  FACE_EMOTION_MISCHIEF,  "I am up to absolutely nothing.", false },
    { VOICE_ACTION_EMOTE_PROUD,     FACE_EMOTION_PROUD,     "I did do rather well, didn't I.", false },
    { VOICE_ACTION_EMOTE_BORED,     FACE_EMOTION_BORED,     "Is that all? Right.",           false },
    { VOICE_ACTION_EMOTE_SMUG,      FACE_EMOTION_SMUG,      "I know. I am rather good.",     false },
    { VOICE_ACTION_EMOTE_SHOCKED,   FACE_EMOTION_SHOCKED,   "No. You are joking.",           false },
    { VOICE_ACTION_EMOTE_DETERMINED,FACE_EMOTION_DETERMINED,"Right. Let us do this.",        false },
    { VOICE_ACTION_EMOTE_GIGGLY,    FACE_EMOTION_GIGGLY,    "Hee. Do not make me laugh.",    false },
    { VOICE_ACTION_EMOTE_SUSPICIOUS,FACE_EMOTION_SUSPICIOUS,"I am watching you.",            false },
    { VOICE_ACTION_EMOTE_RELIEVED,  FACE_EMOTION_RELIEVED,  "Oh good. That was close.",      false },
    { VOICE_ACTION_EMOTE_AMAZED,    FACE_EMOTION_AMAZED,    "Wow. How did you do that?",     false },
    { VOICE_ACTION_EMOTE_NERVOUS,   FACE_EMOTION_NERVOUS,   "Is everything alright?",        false },
    { VOICE_ACTION_EMOTE_PLEADING,  FACE_EMOTION_PLEADING,  "Please? Just a little one?",    false },
    { VOICE_ACTION_EMOTE_TRIUMPHANT,FACE_EMOTION_TRIUMPHANT,"We did it! I knew we would.",   false },
    { VOICE_ACTION_EMOTE_UNIMPRESSED,FACE_EMOTION_UNIMPRESSED,"Is that supposed to impress me?", false },
    { VOICE_ACTION_EMOTE_SCRUNCH,   FACE_EMOTION_SCRUNCH,   "Ugh. That was a lot.",          false },
    { VOICE_ACTION_EMOTE_SHY,       FACE_EMOTION_SHY,       "Oh. Do not look at me.",        false },
    { VOICE_ACTION_EMOTE_PANIC,     FACE_EMOTION_PANIC,     "Everything is fine! Probably!", false },
    { VOICE_ACTION_EMOTE_DISGUSTED, FACE_EMOTION_DISGUSTED, "Take it away. Please.",         false },
    { VOICE_ACTION_EMOTE_SINGING,   FACE_EMOTION_SINGING,   "La la la. I know one song.",    false },
    { VOICE_ACTION_EMOTE_STARRY,    FACE_EMOTION_STARRY,    "You are the best thing on this desk.", false },
    { VOICE_ACTION_EMOTE_SCEPTICAL, FACE_EMOTION_SCEPTICAL, "Is that right. Go on.",          false },
    { VOICE_ACTION_EMOTE_SHEEPISH,  FACE_EMOTION_SHEEPISH,  "It was not entirely my fault.",  false },
    { VOICE_ACTION_EMOTE_HOPEFUL,   FACE_EMOTION_HOPEFUL,   "Do you think so? Really?",       false },
    { VOICE_ACTION_EMOTE_THOUGHTFUL,FACE_EMOTION_THOUGHTFUL,"Give me a moment with that.",    false },
    { VOICE_ACTION_EMOTE_CHEEKY,    FACE_EMOTION_CHEEKY,    "You started it.",                true  },
    { VOICE_ACTION_EMOTE_ALARMED,   FACE_EMOTION_ALARMED,   "What was that? What was that!",  false },
    { VOICE_ACTION_EMOTE_COSY,      FACE_EMOTION_COSY,      "This is nice. Let us stay here.", false },
    { VOICE_ACTION_EMOTE_DREAMY,    FACE_EMOTION_DREAMY,    "Imagine having legs.",           false },
    { VOICE_ACTION_EMOTE_FOCUSED,   FACE_EMOTION_FOCUSED,   "Right. Nothing else exists.",    false },
    { VOICE_ACTION_EMOTE_PLAYFUL,   FACE_EMOTION_PLAYFUL,   "Oh, we are doing this?",         true  },
    { VOICE_ACTION_EMOTE_EXHAUSTED, FACE_EMOTION_EXHAUSTED, "I have been up since you plugged me in.", false },
    { VOICE_ACTION_EMOTE_NEUTRAL,   FACE_EMOTION_NEUTRAL,   "Back to normal.",               false },
};

/* ------------------------------------------------------------------------ */
/* Deciding things, and the kitchen timer                                    */
/* ------------------------------------------------------------------------ */

/*
 * Coin and dice results are written out as whole fixed sentences rather than
 * assembled from a number, so that each one can have a recorded clip and be
 * spoken aloud with no network. "You rolled a %d" would be mute offline.
 */
static const char *const s_coin_faces[] = {
    "Heads.",
    "Tails.",
};

static const char *const s_dice_faces[] = {
    "You rolled a one.",
    "You rolled a two.",
    "You rolled a three.",
    "You rolled a four.",
    "You rolled a five.",
    "You rolled a six.",
};

static void answer_coin_flip(voice_reply_t *reply)
{
    const uint32_t side = esp_random() % 2u;

    reply->emotion = (side == 0) ? FACE_EMOTION_SMUG : FACE_EMOTION_GIGGLY;
    reply_set_text(reply, "%s", s_coin_faces[side]);
}

static void answer_dice_roll(voice_reply_t *reply)
{
    const uint32_t face = esp_random() % 6u;

    reply->emotion = (face >= 4) ? FACE_EMOTION_EXCITED : FACE_EMOTION_HAPPY;
    reply_set_text(reply, "%s", s_dice_faces[face]);
}

/*
 * Timer confirmations are fixed sentences for the same reason: one per
 * duration the recogniser knows, so each gets a clip and is spoken offline.
 */
static const char *confirmation_for_minutes(uint16_t minutes)
{
    switch (minutes) {
    case 1:  return "Timer set for one minute.";
    case 2:  return "Timer set for two minutes.";
    case 5:  return "Timer set for five minutes.";
    case 10: return "Timer set for ten minutes.";
    case 20: return "Timer set for twenty minutes.";
    case 30: return "Timer set for thirty minutes.";
    default: return "Timer set.";
    }
}

static void answer_timer_set(const char *phrase, voice_reply_t *reply)
{
    uint16_t minutes = 0;

    if (!buddy_timer_minutes_from_phrase(phrase, &minutes) ||
        buddy_timer_start(minutes) != ESP_OK) {
        reply->emotion = FACE_EMOTION_CONFUSED;
        reply_set_text(reply, "I did not catch how long for.");
        return;
    }

    reply->emotion = FACE_EMOTION_DETERMINED;
    reply_set_text(reply, "%s", confirmation_for_minutes(minutes));
}

static void answer_timer_check(voice_reply_t *reply)
{
    char remaining[64];

    if (!buddy_timer_describe_remaining(remaining, sizeof(remaining))) {
        reply->emotion = FACE_EMOTION_CURIOUS;
        reply_set_text(reply, "There is no timer running.");
        return;
    }

    reply->emotion = FACE_EMOTION_NEUTRAL;
    reply_set_text(reply, "%s", remaining);

    /*
     * Said as "About four minutes left" rather than read out to the second.
     *
     * The screen has the exact figure and the badge is counting down in the
     * corner; someone who asks out loud wants a sense of it, not a stopwatch.
     * Under a minute it switches to seconds, where the exact number IS the
     * useful answer.
     */
    const uint32_t total   = buddy_timer_seconds_remaining();
    const uint32_t minutes = total / 60u;
    const uint32_t seconds = total % 60u;

    voice_speech_plan_t *plan = &reply->speech;

    if (minutes == 0u) {
        voice_speech_plan_add_number(plan, (int)seconds);
        voice_speech_plan_add(plan, (seconds == 1u) ? VOICE_FRAGMENT_SECOND_LEFT
                                                    : VOICE_FRAGMENT_SECONDS_LEFT);
    } else if (seconds == 0u) {
        voice_speech_plan_add_number(plan, (int)minutes);
        voice_speech_plan_add(plan, (minutes == 1u) ? VOICE_FRAGMENT_MINUTE_LEFT
                                                    : VOICE_FRAGMENT_MINUTES_LEFT);
    } else {
        voice_speech_plan_add(plan, VOICE_FRAGMENT_ABOUT);
        voice_speech_plan_add_number(plan, (int)minutes);
        voice_speech_plan_add(plan, VOICE_FRAGMENT_MINUTES_AND);
        voice_speech_plan_add_number(plan, (int)seconds);
        voice_speech_plan_add(plan, VOICE_FRAGMENT_SECONDS_LEFT);
    }
}

static void answer_timer_cancel(voice_reply_t *reply)
{
    if (!buddy_timer_cancel()) {
        reply->emotion = FACE_EMOTION_CURIOUS;
        reply_set_text(reply, "There is no timer running.");
        return;
    }

    reply->emotion = FACE_EMOTION_RELIEVED;
    reply_set_text(reply, "Timer cancelled.");
}

/**
 * @brief Answer a request for a particular expression.
 *
 * @return true when @p action was an expression request and @p reply was filled.
 */
static bool answer_emote_request(voice_action_t action, voice_reply_t *reply)
{
    const size_t count = sizeof(s_emote_requests) / sizeof(s_emote_requests[0]);

    for (size_t i = 0; i < count; i++) {
        if (s_emote_requests[i].action != action) {
            continue;
        }
        reply_reset(reply);
        reply->emotion    = s_emote_requests[i].emotion;
        reply->play_antic = s_emote_requests[i].play_antic;
        reply_set_text(reply, "%s", s_emote_requests[i].line);
        return true;
    }
    return false;
}

voice_action_t voice_local_brain_action_for_emotion(face_emotion_t emotion)
{
    const size_t count = sizeof(s_emote_requests) / sizeof(s_emote_requests[0]);

    for (size_t i = 0; i < count; i++) {
        if (s_emote_requests[i].emotion == emotion) {
            return s_emote_requests[i].action;
        }
    }
    return VOICE_ACTION_NONE;
}

bool voice_local_brain_try_math(const char *question, voice_reply_t *reply)
{
    if (question == NULL || reply == NULL) {
        return false;
    }

    char result[VOICE_MATH_ANSWER_MAX_LEN];
    if (!voice_math_try_answer(question, result, sizeof(result))) {
        return false;
    }

    reply_reset(reply);
    reply->emotion = FACE_EMOTION_HAPPY;
    reply_set_text(reply, "That's %s.", result);

    /*
     * Say the answer aloud when it is a plain whole number small enough to have
     * a recording. Fractions and large results are shown but not spoken —
     * "three point five" would need a whole second vocabulary, and a sum that
     * comes out to 4913 is being read off the screen anyway.
     */
    bool speakable = (result[0] != '\0');
    for (const char *c = result; *c != '\0'; c++) {
        if (*c < '0' || *c > '9') {
            speakable = false;
            break;
        }
    }

    if (speakable) {
        voice_speech_plan_add(&reply->speech, VOICE_FRAGMENT_THATS);
        voice_speech_plan_add_number(&reply->speech, atoi(result));
    }

    ESP_LOGI(TAG, "Answered a sum locally: %s = %s", question, result);
    return true;
}

bool voice_local_brain_answer(voice_action_t action, const char *phrase, voice_reply_t *reply)
{
    if (reply == NULL) {
        return false;
    }

    /*
     * Arithmetic is the one action whose answer depends on the exact words that
     * matched, so it is handled before the table below rather than inside it.
     */
    if (action == VOICE_ACTION_DO_MATH) {
        if (voice_local_brain_try_math(phrase, reply)) {
            return true;
        }
        /*
         * A registered sum that the parser cannot read is a mistake in
         * voice_commands.c, not something the user did wrong — say something
         * harmless and let the log carry the detail.
         */
        ESP_LOGW(TAG, "Could not work out the registered sum: %s",
                 (phrase != NULL) ? phrase : "(none)");
        reply_reset(reply);
        reply->emotion = FACE_EMOTION_CONFUSED;
        reply_set_text(reply, "I lost my place doing that sum.");
        return true;
    }

    /* Expressions come from a table rather than the switch below. */
    if (answer_emote_request(action, reply)) {
        return true;
    }

    reply_reset(reply);

    switch (action) {

    /* --- Small talk --------------------------------------------------- */

    case VOICE_ACTION_GREETING:
        reply->emotion = FACE_EMOTION_HAPPY;
        reply_set_text(reply, "%s", PICK(s_greetings));
        break;

    case VOICE_ACTION_HOW_ARE_YOU:
        reply->emotion = FACE_EMOTION_LOVE;
        reply_set_text(reply, "%s", PICK(s_how_are_you));
        break;

    case VOICE_ACTION_WHAT_CAN_YOU_DO:
        answer_capabilities(reply);
        break;

    case VOICE_ACTION_TELL_JOKE:
        reply->emotion = FACE_EMOTION_EXCITED;
        reply_set_text(reply, "%s", PICK(s_jokes));
        break;

    case VOICE_ACTION_WHO_MADE_YOU:
        reply->emotion = FACE_EMOTION_HAPPY;
        reply_set_text(reply, "%s", PICK(s_who_made_you));
        break;

    case VOICE_ACTION_GOODBYE:
        reply->emotion = FACE_EMOTION_SLEEPY;
        reply_set_text(reply, "%s", PICK(s_goodbyes));
        break;

    /* --- Questions the hardware can answer ----------------------------- */

    case VOICE_ACTION_TELL_TIME:
        answer_time(reply);
        break;

    case VOICE_ACTION_TIMER_SET:
        answer_timer_set(phrase, reply);
        break;

    case VOICE_ACTION_TIMER_CHECK:
        answer_timer_check(reply);
        break;

    case VOICE_ACTION_TIMER_CANCEL:
        answer_timer_cancel(reply);
        break;

    case VOICE_ACTION_FLIP_COIN:
        answer_coin_flip(reply);
        break;

    case VOICE_ACTION_ROLL_DICE:
        answer_dice_roll(reply);
        break;

    case VOICE_ACTION_ROCK_PAPER_SCISSORS:
        reply->emotion = FACE_EMOTION_MISCHIEF;
        reply_set_text(reply, "%s", PICK(s_rock_paper_scissors));
        break;

    case VOICE_ACTION_ORACLE:
        reply->emotion = FACE_EMOTION_DETERMINED;
        reply_set_text(reply, "%s", PICK(s_oracle_answers));
        break;

    /* --- Conversation --------------------------------------------------- */

    case VOICE_ACTION_YOUR_NAME:
        reply->emotion = FACE_EMOTION_SHY;
        reply_set_text(reply, "%s", PICK(s_names));
        break;

    case VOICE_ACTION_HOW_OLD_ARE_YOU:
        reply->emotion = FACE_EMOTION_CURIOUS;
        reply_set_text(reply, "%s", PICK(s_ages));
        break;

    case VOICE_ACTION_ARE_YOU_A_ROBOT:
        reply->emotion = FACE_EMOTION_SMUG;
        reply_set_text(reply, "%s", PICK(s_robot_answers));
        break;

    case VOICE_ACTION_THANK_YOU:
        reply->emotion = FACE_EMOTION_LOVE;
        reply_set_text(reply, "%s", PICK(s_thanks_answers));
        break;

    case VOICE_ACTION_COMPLIMENT:
        reply->emotion = FACE_EMOTION_SHY;
        reply_set_text(reply, "%s", PICK(s_compliment_answers));
        break;

    case VOICE_ACTION_GOOD_MORNING:
        reply->emotion = FACE_EMOTION_HAPPY;
        reply_set_text(reply, "%s", PICK(s_good_mornings));
        break;

    case VOICE_ACTION_HIGH_FIVE:
        reply->emotion = FACE_EMOTION_TRIUMPHANT;
        reply->play_antic = true;
        reply_set_text(reply, "%s", PICK(s_high_fives));
        break;

    case VOICE_ACTION_TELL_FACT:
        reply->emotion = FACE_EMOTION_EXCITED;
        reply_set_text(reply, "%s", PICK(s_facts));
        break;

    case VOICE_ACTION_COUNT_TO_TEN:
        reply->emotion = FACE_EMOTION_DETERMINED;
        reply_set_text(reply, "%s", PICK(s_counting));
        break;

    /* --- Questions about itself ------------------------------------------ */

    case VOICE_ACTION_HOW_LONG_AWAKE:
        answer_uptime(reply);
        break;

    case VOICE_ACTION_HOW_IS_YOUR_MEMORY:
        answer_memory(reply);
        break;

    case VOICE_ACTION_NETWORK_STATUS:
        answer_network(reply);
        break;

    case VOICE_ACTION_WHAT_IS_WAKE_WORD:
        reply->emotion = FACE_EMOTION_LISTENING;
        reply_set_text(reply, "%s", PICK(s_wake_word_answers));
        break;

    case VOICE_ACTION_GIVE_ADVICE:
        reply->emotion = FACE_EMOTION_THOUGHTFUL;
        reply_set_text(reply, "%s", PICK(s_advice));
        break;

    case VOICE_ACTION_TELL_FORTUNE:
        reply->emotion = FACE_EMOTION_DREAMY;
        reply_set_text(reply, "%s", PICK(s_fortunes));
        break;

    case VOICE_ACTION_TELL_STORY:
        reply->emotion = FACE_EMOTION_COSY;
        reply_set_text(reply, "%s", PICK(s_stories));
        break;

    case VOICE_ACTION_ENCOURAGE_ME:
        reply->emotion = FACE_EMOTION_HOPEFUL;
        reply_set_text(reply, "%s", PICK(s_encouragement));
        break;

    case VOICE_ACTION_TELL_DATE:
        answer_date(reply);
        break;

    case VOICE_ACTION_TELL_TEMPERATURE:
        answer_temperature(reply);
        break;

    case VOICE_ACTION_TELL_HUMIDITY:
        answer_humidity(reply);
        break;

    /* --- Music ---------------------------------------------------------- */

    case VOICE_ACTION_MUSIC_PLAY:
        handle_music_play(reply);
        break;

    case VOICE_ACTION_MUSIC_NEXT:
        handle_music_next(reply);
        break;

    case VOICE_ACTION_MUSIC_STOP:
        audio_music_stop();
        reply->emotion = FACE_EMOTION_NEUTRAL;
        reply->speak_aloud = false;
        reply_set_text(reply, "Music stopped.");
        break;

    /* --- Expressions on request ---------------------------------------- */

    /* --- Device control -------------------------------------------------- */

    case VOICE_ACTION_VOLUME_UP:
        handle_volume(reply, +1);
        break;

    case VOICE_ACTION_VOLUME_DOWN:
        handle_volume(reply, -1);
        break;

    case VOICE_ACTION_OPEN_SETTINGS:
        reply->emotion = FACE_EMOTION_NEUTRAL;
        reply->open_settings = true;
        reply->speak_aloud = false;
        reply_set_text(reply, "Here are your settings.");
        break;

    case VOICE_ACTION_OPEN_CALCULATOR:
        reply->emotion = FACE_EMOTION_FOCUSED;
        reply->open_calculator = true;
        reply->speak_aloud = false;   /* The keypad is the answer, not a sentence. */
        reply_set_text(reply, "Type your sum and I will say the answer.");
        break;

    default:
        ESP_LOGD(TAG, "No offline answer for action %d", (int)action);
        return false;
    }

    ESP_LOGI(TAG, "Answering: %s", reply->text);
    return true;
}

void voice_local_brain_compose_offline_apology(const char *reason, voice_reply_t *reply)
{
    if (reply == NULL) {
        return;
    }
    reply_reset(reply);

    reply->emotion = FACE_EMOTION_DIZZY;

    /*
     * Not spoken aloud, and this one really is the exception.
     *
     * The text here is assembled from a failure reason that varies, so there is
     * no fixed sentence to have recorded a clip for. Every other reply in this
     * file is a whole fixed sentence precisely so it can be spoken with no
     * network at all.
     */
    reply->speak_aloud = false;

    if (reason != NULL && reason[0] != '\0') {
        reply_set_text(reply, "I tried to look that up and could not: %s.", reason);
    } else {
        reply_set_text(reply, "I tried to look that up and could not.");
    }
}

/*
 * What the buddy says when it did not recognise a phrase and is not going to
 * ask anyone.
 *
 * This is the single most-heard reply on a box with no online brain, so it
 * matters more than most. It used to read "I did not catch that, and I cannot
 * ask for help: my online brain is switched off" — which is an advert for a
 * paid feature dressed up as an answer, delivered every time you misspoke.
 *
 * These say what the buddy CAN do instead, and because each is a whole fixed
 * sentence, each has a recorded clip and is spoken out loud.
 */
static const char *const s_did_not_understand[] = {
    "That one is not in my vocabulary. Ask what I can do and I will show you the list.",
    "Missed that. Tap the gear for every phrase I know.",
    "I only know set phrases, and that was not one of them. Try, what can you do.",
    "No idea what that was. Ask me for a joke instead.",
    "Not one of mine. Say, what can you do, and I will show you.",
};

void voice_local_brain_compose_did_not_understand(voice_reply_t *reply)
{
    if (reply == NULL) {
        return;
    }
    reply_reset(reply);

    reply->emotion = FACE_EMOTION_CONFUSED;
    reply->speak_aloud = true;
    reply_set_text(reply, "%s", PICK(s_did_not_understand));
}
