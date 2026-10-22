/*
 * voice_assistant.c — Deciding what the buddy does next.
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "audio_music.h"
#include "audio_prompts.h"
#include "audio_voice.h"
#include "buddy_presence.h"
#include "buddy_timer.h"
#include "buddy_config.h"
#include "buddy_settings.h"
#include "face_animator.h"
#include "hw_display.h"
#include "hw_speaker.h"
#include "ui_calculator.h"
#include "ui_screen_face.h"
#include "ui_speech_bubble.h"
#include "voice_assistant.h"
#include "voice_cloud_brain.h"
#include "voice_local_brain.h"
#include "voice_recorder.h"
#include "voice_router.h"
#include "voice_speech_gate.h"
#include "voice_wakeword.h"

static const char *TAG = "assistant";

/** How long a caption stays up when nothing follows it. */
#define CAPTION_LINGER_MS       5000

/** How long the "listening" caption stays up if nothing is heard. */
#define LISTENING_CAPTION_MS    8000

/**
 * True when music was playing and got ducked for a conversation, so it can be
 * brought back afterwards.
 */
static bool s_music_was_ducked;

/** Set when a cloud request reports a failure, so the chime plays once at the end. */
static bool s_cloud_request_failed;

/** How many cloud requests have been spent in the current hour. */
static voice_budget_t s_budget;

/**
 * When the cloud request currently in flight was dispatched, or 0 for none.
 *
 * The watchdog's original rule was "holds are justified while a request is in
 * flight", which is true right up until the request stops being in flight in
 * any useful sense — a TLS handshake that never completes, a socket that never
 * answers. The component gives no way to set a timeout, so from out here such
 * a call simply never returns, the holds never lift, and the buddy is deaf
 * until it is power cycled. This timestamp is what turns "in flight" into a
 * question with an answer.
 */
static volatile int64_t s_request_started_us;

/** Set once the ears have been given back on a request that is still running. */
static volatile bool s_request_ears_returned;

/**
 * Set when a request has outlived its deadline and its results must be ignored.
 *
 * Without this, a reply that turns up two minutes late would take the speaking
 * hold, start talking over whatever the buddy is doing by then, and set an
 * expression for a question the user has long forgotten asking.
 */
static volatile bool s_request_abandoned;

/** When the current listening window opened, so a stuck one can be spotted. */
static volatile int64_t s_listening_since_us;

/* Defined with the watchdog further down; used by the dispatch sites above it. */
static void request_started(void);
static void request_finished(void);

uint16_t voice_assistant_requests_remaining(void)
{
    const uint16_t allowance = buddy_settings_get()->requests_per_hour;

    if (allowance == 0) {
        return UINT16_MAX;   /* No ceiling was set. */
    }
    return voice_budget_remaining(&s_budget, esp_timer_get_time(), allowance);
}

bool voice_assistant_spend_request(void)
{
    return voice_budget_try_spend(&s_budget, esp_timer_get_time(),
                                  buddy_settings_get()->requests_per_hour);
}

/* ------------------------------------------------------------------------ */
/* The worker task                                                           */
/*                                                                           */
/* Some of what the buddy does arrives on a task that must not be blocked.   */
/* The kitchen timer's callback runs on the esp_timer SERVICE task — the one */
/* shared by the presence radar, the Wi-Fi retry deadline, and every timer   */
/* inside ESP-IDF itself. Playing a chime there stops all of them for the    */
/* length of the chime, and the Wi-Fi deadline that exists to escape a stuck */
/* "Connecting..." is exactly the thing you cannot afford to stall.          */
/*                                                                           */
/* So work that talks to the speaker is handed to this task instead. It also */
/* runs the watchdog below, because a task that wakes on a timeout is the    */
/* natural place for "has anything been stuck for too long?".                */
/* ------------------------------------------------------------------------ */

/** What the worker can be asked to do. */
typedef enum {
    ASSISTANT_JOB_TIMER_EXPIRED = 0,   /*!< The kitchen timer reached zero   */
    ASSISTANT_JOB_TYPED_COMMAND,       /*!< A phrase sent over the console   */
} assistant_job_kind_t;

typedef struct {
    assistant_job_kind_t kind;
    voice_action_t       action;       /*!< ASSISTANT_JOB_TYPED_COMMAND only */
    const char          *phrase;       /*!< Points into the vocabulary table */
} assistant_job_t;

static QueueHandle_t s_job_queue;

/** How often the worker wakes to check for a stuck state. */
#define WATCHDOG_TICK_MS            1000

/**
 * How long listening may stay held with nothing to justify it.
 *
 * Long enough that no ordinary chime, clip or cloud round trip trips it, short
 * enough that a leak costs a pause rather than the rest of the session. Cloud
 * requests are excluded from the count entirely — they can legitimately hold
 * for the length of a network timeout — so this is really "held, silent, and
 * not waiting on anything".
 */
#define WATCHDOG_STUCK_AFTER_TICKS  6

/* ------------------------------------------------------------------------ */
/* Small helpers                                                             */
/* ------------------------------------------------------------------------ */

/**
 * @brief Pause any music so the buddy can hear and be heard.
 */
static void duck_music(void)
{
    if (audio_music_is_playing()) {
        audio_music_pause();
        s_music_was_ducked = true;
        ESP_LOGD(TAG, "Music paused for the conversation");
    }
}

/**
 * @brief Bring the music back if this conversation interrupted it.
 */
static void restore_music(void)
{
    if (s_music_was_ducked) {
        s_music_was_ducked = false;
        audio_music_resume();
        ESP_LOGD(TAG, "Music resumed");
    }
}

/**
 * @brief Play a chime without the microphone hearing it.
 *
 * Takes and releases only its OWN hold. That is the whole point of named holds:
 * this used to set a shared flag false on the way out, which released whatever
 * else was relying on the microphone staying quiet — most damagingly a cloud
 * request whose recording was still being uploaded.
 */
static void play_chime(audio_prompt_t prompt)
{
    voice_wakeword_hold(VOICE_HOLD_CHIME);
    audio_prompts_play(prompt);
    voice_wakeword_release(VOICE_HOLD_CHIME);
}

/* ------------------------------------------------------------------------ */
/* Saying things                                                             */
/* ------------------------------------------------------------------------ */

bool voice_assistant_deliver_reply(const voice_reply_t *reply)
{
    if (reply == NULL) {
        return false;
    }

    /* The expression comes first — it lands before the words are read. */
    ui_screen_face_set_emotion(reply->emotion);

    if (reply->text[0] != '\0') {
        ui_screen_face_show_caption(UI_SPEECH_FROM_BUDDY, reply->text, CAPTION_LINGER_MS);
    }

    if (reply->play_antic) {
        ui_screen_face_play_antic();
    }

    if (reply->open_settings) {
        ui_screen_face_open_settings();
    }

    if (reply->open_examples) {
        ui_screen_face_open_examples();
    }

    if (reply->open_calculator) {
        ui_calculator_open();
    }

    if (!reply->speak_aloud || reply->text[0] == '\0') {
        return false;
    }

    /*
     * Two voices, best first.
     *
     * The online one can say anything and sounds like a person, so it wins
     * whenever it is reachable. Falling back to the recorded clips only when it
     * is not means adding an API key upgrades the voice without changing
     * anything else about how the buddy behaves.
     */
    if (voice_cloud_brain_is_available()) {
        /*
         * Hold BEFORE dispatching, release if the dispatch is refused.
         *
         * The other order looks harmless and is not: the cloud brain runs on
         * its own task at the same priority, so a request that fails
         * immediately can reach its completion callback — and release the
         * hold — before this line ever runs. The hold then sticks with nothing
         * left to clear it, and the buddy is deaf until the watchdog notices.
         */
        voice_wakeword_hold(VOICE_HOLD_SPEAKING);

        if (voice_cloud_brain_speak(reply->text) == ESP_OK) {
            /* Under the same deadline as a question — the network is the network. */
            request_started();
            return true;   /* Audio is on its way; the caller should not resume music. */
        }

        voice_wakeword_release(VOICE_HOLD_SPEAKING);
    }

    /*
     * The offline voice: a recorded clip of this exact reply, baked into flash
     * at build time. Only the answers the buddy knows by heart have one — see
     * audio_voice.h — so a miss here is normal and simply means the caption is
     * the whole reply, as it always used to be.
     */
    /* Same ordering rule: hold first, release if the clip did not start. */
    voice_wakeword_hold(VOICE_HOLD_SPEAKING);

    if (audio_voice_speak_reply(reply)) {
        ui_screen_face_set_speaking(true);
        return true;
    }

    voice_wakeword_release(VOICE_HOLD_SPEAKING);
    return false;
}

/* ------------------------------------------------------------------------ */
/* Reacting to the speech engine                                             */
/* ------------------------------------------------------------------------ */

/**
 * @brief The wake word was heard: get ready to listen.
 */
static void handle_wake_word(void)
{
    ESP_LOGI(TAG, "Waking up");

    duck_music();

    ui_screen_face_set_emotion(FACE_EMOTION_LISTENING);
    ui_screen_face_show_caption(UI_SPEECH_STATUS, "Listening...", LISTENING_CAPTION_MS);

    play_chime(AUDIO_PROMPT_WAKE);
}

/**
 * @brief One of the offline phrases matched: answer it straight away.
 */
static void handle_recognised_command(const voice_event_t *event)
{
    ESP_LOGI(TAG, "Command: %s", (event->phrase != NULL) ? event->phrase : "(unknown)");

    /* Echo what was heard, so a misheard phrase is obvious rather than baffling. */
    if (event->phrase != NULL) {
        ui_screen_face_show_caption(UI_SPEECH_FROM_USER, event->phrase, 2500);
    }

    voice_reply_t reply;
    if (!voice_local_brain_answer(event->action, event->phrase, &reply)) {
        /* The phrase is in the table but nothing handles it — a coding slip. */
        ESP_LOGW(TAG, "No handler for action %d", (int)event->action);
        voice_local_brain_compose_offline_apology(NULL, &reply);
    }

    /*
     * Change the face BEFORE the chime, not after.
     *
     * The chime is played synchronously — the task sits inside the WAV until
     * the last sample has gone out of the speaker. Doing that first means the
     * face does not move until the chime finishes, which is the whole of the
     * "it feels laggy" complaint. Reacting first costs nothing and makes the
     * response land the instant the phrase is recognised.
     */
    ui_screen_face_set_emotion(reply.emotion);

    /*
     * Music commands take over the speaker on purpose, so the paused track
     * should stay paused rather than fighting the new one. A spoken reply also
     * needs the speaker, and restores the music itself once it finishes.
     */
    const bool reply_took_over_audio =
        (event->action == VOICE_ACTION_MUSIC_PLAY) ||
        (event->action == VOICE_ACTION_MUSIC_NEXT) ||
        (event->action == VOICE_ACTION_MUSIC_STOP);

    /*
     * No chime once a track is already playing. This is the v3.7.1 crash fix.
     *
     * voice_local_brain_answer() above does not merely describe what to do, it
     * DOES it: handling "sing me a song" starts the MP3 before returning. The
     * chime is not a notification — it is a blocking WAV that reconfigures the
     * I2S channel and then writes samples into it from this task. Playing one
     * now reconfigures that channel underneath a running decoder and puts two
     * writers on it, which is why asking for a song sometimes took the box down
     * and why it looked like one particular track's fault: the track is chosen
     * at random, so whichever came up got the blame.
     *
     * Starting the music IS the acknowledgement, so there was never anything
     * for the ding to add. Stopping is different — nothing is playing after it,
     * and silence with no confirmation is the one case that needs a sound.
     */
    const bool music_now_playing =
        (event->action == VOICE_ACTION_MUSIC_PLAY) ||
        (event->action == VOICE_ACTION_MUSIC_NEXT);

    if (!music_now_playing) {
        play_chime(AUDIO_PROMPT_UNDERSTOOD);
    }

    const bool audio_is_coming = voice_assistant_deliver_reply(&reply);

    if (reply_took_over_audio) {
        s_music_was_ducked = false;

        /*
         * Sing along.
         *
         * The loudness meter that drives the mouth sits in the middle of the
         * speaker's write path, so it is already measuring the music — nothing
         * knew to ask it to. Turning the mouth animation on here means the
         * buddy mouths along to the track instead of wearing a fixed grin, and
         * playback_finished() switches it off again at the end of the song
         * exactly as it does after a spoken reply.
         */
        if (event->action != VOICE_ACTION_MUSIC_STOP && hw_speaker_is_playing()) {
            ui_screen_face_set_speaking(true);
        }
    } else if (!audio_is_coming) {
        restore_music();
    }
}

/**
 * @brief The listening window closed without a match: try the cloud.
 */
/**
 * @brief Say "that was not one of mine" and stop. The offline outcome.
 */
static void answer_without_asking(void)
{
    voice_reply_t reply;
    voice_local_brain_compose_did_not_understand(&reply);

    if (!voice_assistant_deliver_reply(&reply)) {
        restore_music();
    }
}

/**
 * @brief Explain why the buddy would have asked but did not, then stop.
 */
static void decline_with_reason(const char *reason)
{
    voice_reply_t reply;
    voice_local_brain_compose_offline_apology(reason, &reply);

    /* Chime before the reply, so the face reacts while the chime rings. */
    play_chime(AUDIO_PROMPT_ERROR);

    if (!voice_assistant_deliver_reply(&reply)) {
        restore_music();
    }
}

static void handle_listen_timeout(void)
{
    const uint8_t *wav_data = NULL;
    size_t wav_length = 0;

    const buddy_settings_t *settings = buddy_settings_get();

    /*
     * Ask the router who should answer this, rather than deciding here.
     *
     * The policy — which mode, how sure the recogniser was, how much of the
     * recording was speech, whether there is any allowance left — lives in
     * voice_router.c as a pure function, where the host tests can put it
     * through every combination. This function's job is only to carry out the
     * verdict.
     */
    const voice_route_inputs_t inputs = {
        /*
         * False, always. Reaching this function at all means the recogniser
         * either matched nothing or matched something too weakly to act on —
         * and voice_wakeword.c has already logged which. A confident match went
         * to handle_recognised_command() and never arrives here.
         */
        .phrase_matched    = false,
        .phrase_confidence = 0.0f,

        .recorded_seconds  = voice_recorder_get_duration_seconds(),
        .speech_seconds    = voice_recorder_get_speech_seconds(),
        .peak_amplitude    = voice_recorder_get_peak_amplitude(),

        .cloud_ready       = voice_cloud_brain_is_available(),
        .mode              = settings->brain_mode,

        .requests_used     = (uint16_t)(settings->requests_per_hour -
                                        voice_assistant_requests_remaining()),
        .requests_allowed  = settings->requests_per_hour,
    };

    const char *reason = NULL;
    const voice_route_t route = voice_router_decide(&inputs, &reason);

    switch (route) {
    case VOICE_ROUTE_NOTHING_HEARD:
        /*
         * Every number the decision used, on one line. When somebody reports
         * that the buddy will not answer them, this is the line that says
         * whether the microphone produced nothing (peak near zero), the window
         * closed too early (a short recording), or the gate is mis-tuned for
         * the room (a healthy peak with no speech seconds) — and those three
         * have completely different fixes.
         */
        ESP_LOGW(TAG, "Nothing to act on: %.2f s recorded, %.2f s of it speech, "
                      "peak %ld (need %d), room %lu, threshold %lu",
                 inputs.recorded_seconds, inputs.speech_seconds,
                 (long)inputs.peak_amplitude, BUDDY_AUDIBLE_PEAK_AMPLITUDE,
                 (unsigned long)voice_speech_gate_room_level(),
                 (unsigned long)voice_speech_gate_threshold());
        ui_screen_face_set_emotion(FACE_EMOTION_CONFUSED);
        ui_screen_face_show_caption(UI_SPEECH_STATUS, "I did not hear anything.", 2500);
        restore_music();
        return;

    case VOICE_ROUTE_ANSWER_OFFLINE:
        ESP_LOGI(TAG, "Answering offline; not worth a cloud request");
        answer_without_asking();
        return;

    case VOICE_ROUTE_DECLINE_NO_CLOUD:
        decline_with_reason(voice_cloud_brain_explain_unavailability());
        return;

    case VOICE_ROUTE_DECLINE_BUDGET:
        ESP_LOGW(TAG, "Hourly allowance of %u questions is spent", settings->requests_per_hour);
        decline_with_reason(reason);
        return;

    case VOICE_ROUTE_ASK_CLOUD:
    default:
        break;
    }

    /* The recording itself. A route to the cloud with nothing to send is a bug. */
    if (voice_recorder_get_wav(&wav_data, &wav_length) != ESP_OK) {
        ESP_LOGW(TAG, "Routed to the cloud but the recording was refused");
        answer_without_asking();
        return;
    }

    if (!voice_assistant_spend_request()) {
        decline_with_reason("I have used up my questions for this hour");
        return;
    }

    ESP_LOGI(TAG, "Sending %.2f s to the cloud (%.2f s of it speech)",
             voice_recorder_get_duration_seconds(),
             voice_recorder_get_speech_seconds());

    ui_screen_face_set_emotion(FACE_EMOTION_THINKING);

    /*
     * A generous expiry rather than none at all.
     *
     * This was the one caption in the firmware created with auto_hide_ms = 0,
     * on the reasoning that the answer always replaces it. It does — as long as
     * a callback fires. If one ever does not, "Thinking..." sits on the screen
     * for the rest of the session with no way to dismiss it, which is a
     * miserable way to represent "something went wrong". Outliving the longest
     * plausible round trip is all it needs to do.
     */
    ui_screen_face_show_caption(UI_SPEECH_STATUS, "Thinking...", 45000);
    play_chime(AUDIO_PROMPT_THINKING);

    s_cloud_request_failed = false;

    /*
     * Stop listening BEFORE handing the work over, and let go again if it is
     * refused. The recording being uploaded lives in the recorder's single
     * buffer, and a second wake word would start overwriting it mid-request.
     */
    voice_wakeword_hold(VOICE_HOLD_THINKING);
    voice_recorder_set_in_use(true);

    if (voice_cloud_brain_ask(wav_data, wav_length) == ESP_OK) {
        request_started();
    } else {
        voice_wakeword_release(VOICE_HOLD_THINKING);
        voice_recorder_set_in_use(false);

        ui_screen_face_set_emotion(FACE_EMOTION_DIZZY);
        ui_screen_face_show_caption(UI_SPEECH_STATUS, "I am still busy with the last one.",
                                    CAPTION_LINGER_MS);
        restore_music();
    }
}

/**
 * @brief Entry point for everything the speech engine reports.
 *
 * @note Runs on the speech engine's event task.
 */
static void speech_event_handler(const voice_event_t *event)
{
    if (event == NULL) {
        return;
    }

    switch (event->type) {
    case VOICE_EVENT_WAKE_WORD:
        /* Stamped so the watchdog can tell a slow user from a stuck window. */
        s_listening_since_us = esp_timer_get_time();
        handle_wake_word();
        break;

    case VOICE_EVENT_COMMAND_MATCHED:
        s_listening_since_us = 0;   /* The window closed properly. */
        handle_recognised_command(event);
        break;

    case VOICE_EVENT_LISTEN_TIMEOUT:
        s_listening_since_us = 0;
        handle_listen_timeout();
        break;

    default:
        break;
    }
}

/* ------------------------------------------------------------------------ */
/* Reacting to the cloud                                                     */
/* ------------------------------------------------------------------------ */

static void cloud_question_transcribed(const char *question)
{
    if (question == NULL || s_request_abandoned) {
        return;
    }

    /*
     * Transcription is the only step that reads the recording, so the buffer is
     * free from here on. Releasing it now — rather than when the whole request
     * ends — is what lets a second wake word record normally while the answer
     * is still being fetched.
     */
    voice_recorder_set_in_use(false);

    ui_screen_face_show_caption(UI_SPEECH_FROM_USER, question, 3000);
}

static void cloud_answer_ready(const char *answer, face_emotion_t emotion)
{
    if (answer == NULL || s_request_abandoned) {
        return;
    }
    ui_screen_face_set_emotion(emotion);
    ui_screen_face_show_caption(UI_SPEECH_FROM_BUDDY, answer, CAPTION_LINGER_MS * 2);
}

/**
 * True once audio for this request is genuinely playing.
 *
 * NOT the same question as "did synthesis return something", which is what the
 * completion handler used to ask. A response object can come back non-NULL with
 * an empty payload, and in that case nothing is ever handed to the speaker — so
 * the finished callback never fires, and the hold that the finished callback
 * was supposed to release is never released. That is a buddy that shows its
 * answer on screen, says nothing, and never hears the wake word again.
 *
 * Set only where playback actually starts, and read by cloud_request_finished()
 * to decide whether the speaker really is taking over.
 */
static volatile bool s_reply_audio_started;

static void cloud_speech_ready(const void *mp3_data, size_t mp3_length)
{
    if (mp3_data == NULL || mp3_length == 0) {
        ESP_LOGW(TAG, "The spoken reply came back empty; the words are on screen only");
        return;   /* cloud_request_finished() tidies up, because nothing started. */
    }

    /*
     * An answer to a question the buddy already gave up on. Playing it would
     * mean talking over whatever it is doing now, minutes later, about
     * something nobody remembers asking.
     */
    if (s_request_abandoned) {
        ESP_LOGW(TAG, "A spoken reply arrived after the request was written off; dropping it");
        return;
    }

    ESP_LOGI(TAG, "Speaking %u bytes of audio", (unsigned)mp3_length);

    /*
     * Listening is already held for the duration of the request. Take the
     * speaking hold too, so that the request finishing does not open the ears
     * while the answer is still being read out.
     */
    voice_wakeword_hold(VOICE_HOLD_SPEAKING);
    ui_screen_face_set_speaking(true);

    if (hw_speaker_play_mp3_memory(mp3_data, mp3_length) != ESP_OK) {
        ESP_LOGW(TAG, "Could not play the spoken reply");
        ui_screen_face_set_speaking(false);
        voice_wakeword_release(VOICE_HOLD_SPEAKING);
        return;
    }

    s_reply_audio_started = true;
}

static void cloud_failed(const char *reason)
{
    /*
     * The buddy has already apologised for this one and moved on. Saying so a
     * second time, minutes later, is worse than saying nothing.
     */
    if (s_request_abandoned) {
        ESP_LOGI(TAG, "A failure arrived for a request already written off");
        return;
    }

    voice_reply_t reply;
    voice_local_brain_compose_offline_apology(reason, &reply);
    reply.emotion = FACE_EMOTION_DIZZY;

    /* Show the apology. The chime and resuming both happen at the end. */
    s_cloud_request_failed = true;
    voice_assistant_deliver_reply(&reply);
}

/**
 * @brief Every cloud request ends here, whether it worked or not.
 *
 * Having one guaranteed exit point is what stops the buddy from ever getting
 * stuck deaf: no matter which of the three cloud steps failed, listening comes
 * back on.
 */
static void cloud_request_finished(bool audio_was_synthesised)
{
    const bool failed = s_cloud_request_failed;
    s_cloud_request_failed = false;

    /*
     * A request the watchdog already wrote off. Everything it would have done
     * here was done at that point — the holds released, the buffer freed, the
     * music resumed — so all that is left is to note that the call finally
     * came back and clear the flag for the next one.
     */
    if (s_request_abandoned) {
        ESP_LOGI(TAG, "The abandoned cloud request finally returned");
        s_request_abandoned = false;
        request_finished();
        return;
    }

    request_finished();

    /*
     * The request itself is over either way, so its hold goes now. If audio is
     * playing, the SPEAKING hold taken in cloud_speech_ready() is what keeps the
     * ears shut, and playback_finished() releases that.
     */
    voice_wakeword_release(VOICE_HOLD_THINKING);

    /*
     * Trust what actually started, not what the cloud brain synthesised.
     *
     * The parameter says "a speech response came back". That was the old test,
     * and it is wrong for an empty payload: nothing reached the speaker, so no
     * finished callback is coming, so returning here left the buddy deaf for
     * good. s_reply_audio_started is set only where playback really began.
     */
    const bool audio_is_playing = s_reply_audio_started;
    s_reply_audio_started = false;

    if (audio_is_playing) {
        /* The speaker takes over from here; playback_finished tidies up. */
        return;
    }

    if (audio_was_synthesised) {
        ESP_LOGW(TAG, "Speech was synthesised but never started playing");
    }

    ui_screen_face_set_speaking(false);

    if (failed) {
        play_chime(AUDIO_PROMPT_ERROR);
    }

    voice_wakeword_release(VOICE_HOLD_SPEAKING);
    restore_music();
}

/* ------------------------------------------------------------------------ */
/* Reacting to the speaker                                                   */
/* ------------------------------------------------------------------------ */

/**
 * @brief Playback of something finished — a spoken reply, or a song.
 */
static void playback_finished(void)
{
    ESP_LOGD(TAG, "Playback finished");

    /* Close the mouth and start listening again. */
    ui_screen_face_set_speaking(false);
    s_reply_audio_started = false;
    voice_wakeword_release(VOICE_HOLD_SPEAKING);

    restore_music();
}

/* ------------------------------------------------------------------------ */
/* The kitchen timer and the radar                                           */
/* ------------------------------------------------------------------------ */

/**
 * @brief The countdown reached zero.
 *
 * Runs on the esp_timer task. Everything it touches — the face, the caption,
 * the speaker — takes its own lock, so there is nothing to arrange here beyond
 * saying the words.
 */
static void announce_timer_expired(void)
{
    voice_reply_t reply = {
        .emotion     = FACE_EMOTION_SHOCKED,
        .speak_aloud = true,
    };
    strncpy(reply.text, "Your timer is up.", sizeof(reply.text) - 1);

    /*
     * Chime first this time, unlike a normal answer. A timer going off is the
     * one moment the buddy needs to interrupt rather than respond, and the
     * chime is what pulls your eyes over before the words start.
     */
    play_chime(AUDIO_PROMPT_WAKE);

    if (!voice_assistant_deliver_reply(&reply)) {
        /* Nothing to say it with, so leave the face and caption showing. */
        restore_music();
    }
}

/**
 * @brief The countdown reached zero. Runs on the esp_timer SERVICE task.
 *
 * Which is why it does nothing here but post a job. Everything this
 * announcement needs — a chime, a spoken clip — blocks for the length of the
 * audio, and blocking the esp_timer task stops the presence radar, the Wi-Fi
 * retry deadline, and every timer inside ESP-IDF along with it. The worker task
 * exists so that a talking buddy never stalls the rest of the system.
 */
static void timer_expired(void)
{
    const assistant_job_t job = { .kind = ASSISTANT_JOB_TIMER_EXPIRED };

    if (s_job_queue == NULL || xQueueSend(s_job_queue, &job, 0) != pdTRUE) {
        ESP_LOGW(TAG, "Could not queue the timer announcement");
    }
}

bool voice_assistant_handle_typed_command(voice_action_t action, const char *phrase)
{
    const assistant_job_t job = {
        .kind   = ASSISTANT_JOB_TYPED_COMMAND,
        .action = action,
        .phrase = phrase,
    };

    return s_job_queue != NULL && xQueueSend(s_job_queue, &job, 0) == pdTRUE;
}

/**
 * @brief Somebody walked up to the desk.
 *
 * Deliberately quiet: the buddy looks up and brightens, and that is all. No
 * chime and no greeting out loud — a box that announces itself every time you
 * sit down stops being charming almost immediately.
 */
static void someone_arrived(void)
{
    /*
     * Never interrupt something already happening. Mid-conversation the buddy
     * is listening for an answer, and mid-playback it is either speaking or
     * playing music — barging in with a new expression during either would
     * look like a glitch rather than a greeting.
     */
    if (voice_wakeword_is_listening() || hw_speaker_is_playing()) {
        return;
    }

    /*
     * Both of these reach into LVGL, and this runs on the esp_timer task rather
     * than the LVGL one. ui_screen_face_set_emotion() takes the display lock
     * itself; face_animator_notice_activity() does not — it can delete and
     * re-arm LVGL timers — so the lock is taken here to cover it.
     */
    ui_screen_face_set_emotion(FACE_EMOTION_CURIOUS);

    if (hw_display_lock(50)) {
        face_animator_notice_activity();
        hw_display_unlock();
    }
}

/* ------------------------------------------------------------------------ */
/* The worker, and the watchdog that keeps the buddy listening               */
/* ------------------------------------------------------------------------ */

/**
 * @brief Note that a cloud request has just been handed over.
 */
static void request_started(void)
{
    s_request_started_us  = esp_timer_get_time();
    s_request_ears_returned = false;
    s_request_abandoned   = false;
}

/**
 * @brief Note that a cloud request is over, however it ended.
 */
static void request_finished(void)
{
    s_request_started_us = 0;
    s_request_ears_returned = false;
    voice_recorder_set_in_use(false);
}

/**
 * @brief Give the microphone back while a slow request carries on without it.
 *
 * The recording has long since been uploaded by this point; what is left is
 * waiting for an answer, and waiting is not a reason to be deaf. The request
 * keeps running — if it succeeds, the answer still arrives and is still spoken.
 *
 * The recording buffer stays claimed, so a wake word in this window is heard
 * and can be answered from the offline vocabulary but is not written over the
 * upload. That is the whole reason this is safe.
 */
static void return_the_ears_early(void)
{
    s_request_ears_returned = true;

    ESP_LOGW(TAG, "Cloud request has run %d seconds; listening again while it finishes",
             BUDDY_LISTENING_RETURNS_AFTER_S);

    voice_wakeword_release(VOICE_HOLD_THINKING);
}

/**
 * @brief Write off a request that is never going to arrive.
 */
static void abandon_request(void)
{
    ESP_LOGE(TAG, "Cloud request gave up after %d seconds. Everything else still works.",
             BUDDY_CLOUD_REQUEST_DEADLINE_S);

    s_request_abandoned = true;
    s_request_started_us = 0;
    s_reply_audio_started = false;
    s_cloud_request_failed = false;

    voice_recorder_set_in_use(false);
    voice_wakeword_release_all();

    ui_screen_face_set_speaking(false);
    ui_screen_face_set_emotion(FACE_EMOTION_DIZZY);
    ui_screen_face_show_caption(UI_SPEECH_STATUS,
                                "That took too long. I am listening again.",
                                CAPTION_LINGER_MS);

    /*
     * Drop the client so the next question builds a fresh one. Whatever state
     * the old connection is in, it is not a state worth reusing.
     */
    voice_cloud_brain_reset_conversation();

    restore_music();
}

/**
 * @brief Notice when the ears have been held shut with nothing to justify it.
 *
 * Every hold is released on a specific path — playback finishing, a request
 * completing, a chime returning. The trouble with that arrangement is that
 * "the buddy silently stopped answering" is what EVERY missed release looks
 * like, whichever one it was, and the user has no way to tell it from the wake
 * word simply not being heard.
 *
 * So rather than trusting that the last bug of this shape has been found, this
 * asks a question that has one right answer: are the ears shut while nothing is
 * playing, nothing is being fetched, and no phrase window is open? Held in that
 * state for several seconds running, something leaked — and the honest response
 * is to open them and say so in the log, not to stay deaf out of politeness to
 * whichever module forgot.
 */
static void watchdog_tick(void)
{
    static uint32_t quiet_ticks;

    const int64_t now = esp_timer_get_time();

    /* --- 1. A request that has run too long ---------------------------- */

    /*
     * Checked BEFORE the "is anything justifying this" test below, and that
     * order is the whole fix. The old watchdog treated a request in flight as
     * justification and stopped there, so a call that never returned was a
     * permanent excuse for staying deaf. A request now has to finish within a
     * deadline to keep that privilege.
     */
    if (s_request_started_us != 0) {
        const int64_t elapsed_us = now - s_request_started_us;

        if (elapsed_us > (int64_t)BUDDY_CLOUD_REQUEST_DEADLINE_S * 1000000LL) {
            abandon_request();
            quiet_ticks = 0;
            return;
        }

        if (!s_request_ears_returned &&
            elapsed_us > (int64_t)BUDDY_LISTENING_RETURNS_AFTER_S * 1000000LL) {
            return_the_ears_early();
        }
    }

    /* --- 2. A listening window that never closed ----------------------- */

    /*
     * The phrase window is six seconds. One still open at four times that is
     * not a user taking their time, it is the detect task having stopped
     * fetching — and like the request above it would otherwise count as a
     * standing excuse for the ears being shut.
     */
    const bool listening = voice_wakeword_is_listening();

    if (listening && s_listening_since_us != 0 &&
        (now - s_listening_since_us) > (int64_t)BUDDY_LISTEN_WINDOW_MS * 4000LL) {
        ESP_LOGW(TAG, "Listening window never closed; cancelling it");
        voice_wakeword_cancel_listening();
        s_listening_since_us = 0;
    }

    /* --- 3. Holds with nothing at all behind them ---------------------- */

    const uint32_t holds = voice_wakeword_get_holds();

    /*
     * A window that has already been force-cancelled above does NOT count.
     * Rule 2 zeroes the timestamp when it gives up on one, so if the detect
     * task is wedged badly enough to ignore the cancel, this stops being an
     * excuse rather than becoming a permanent one.
     */
    const bool listening_recently = listening && (s_listening_since_us != 0);

    const bool something_justifies_it = (holds == 0) ||
                                        hw_speaker_is_playing() ||
                                        (s_request_started_us != 0) ||
                                        listening_recently;

    if (something_justifies_it) {
        quiet_ticks = 0;
        return;
    }

    if (++quiet_ticks < WATCHDOG_STUCK_AFTER_TICKS) {
        return;
    }

    ESP_LOGW(TAG, "Listening has been held shut for %u seconds with nothing "
                  "playing (holds 0x%02lx). Opening the ears.",
             (unsigned)(quiet_ticks * WATCHDOG_TICK_MS / 1000), (unsigned long)holds);

    quiet_ticks = 0;
    s_reply_audio_started = false;
    s_cloud_request_failed = false;

    voice_wakeword_release_all();
    ui_screen_face_set_speaking(false);
    restore_music();
}

static void assistant_worker_task(void *argument)
{
    (void)argument;

    while (true) {
        assistant_job_t job;

        if (xQueueReceive(s_job_queue, &job, pdMS_TO_TICKS(WATCHDOG_TICK_MS)) == pdTRUE) {
            switch (job.kind) {
            case ASSISTANT_JOB_TIMER_EXPIRED:
                announce_timer_expired();
                break;
            case ASSISTANT_JOB_TYPED_COMMAND: {
                /*
                 * The same handler the microphone path uses, so a typed phrase
                 * gets the same caption, face, reply and music handling. It
                 * blocks for the length of any chime, which is what this task
                 * is for.
                 */
                const voice_event_t event = {
                    .type   = VOICE_EVENT_COMMAND_MATCHED,
                    .action = job.action,
                    .phrase = job.phrase,
                };
                handle_recognised_command(&event);
                break;
            }
            default:
                break;
            }
        }

        watchdog_tick();
    }
}

/* ------------------------------------------------------------------------ */
/* Start-up                                                                  */
/* ------------------------------------------------------------------------ */

esp_err_t voice_assistant_start(void)
{
    voice_local_brain_init();

    ESP_RETURN_ON_ERROR(voice_recorder_init(), TAG, "recorder failed");

    /* Learn when audio stops so the mouth closes and listening resumes. */
    hw_speaker_set_finished_callback(playback_finished);

    /*
     * The worker. Started before anything that can post to it, and before the
     * watchdog could ever be needed.
     */
    s_job_queue = xQueueCreate(4, sizeof(assistant_job_t));
    ESP_RETURN_ON_FALSE(s_job_queue != NULL, ESP_ERR_NO_MEM, TAG, "no memory for the job queue");

    const BaseType_t worker_created =
        xTaskCreatePinnedToCore(assistant_worker_task, "assistant", 4 * 1024,
                                NULL, 4, NULL, 0);
    ESP_RETURN_ON_FALSE(worker_created == pdPASS, ESP_FAIL, TAG, "worker task failed to start");

    /* The kitchen timer, and the radar that notices you arriving. */
    buddy_timer_init();
    buddy_timer_set_expired_callback(timer_expired);
    buddy_presence_start(someone_arrived);

    /* The cloud brain is started even without a key: it reports as unavailable
     * until one is entered, and then simply starts working with no restart. */
    const voice_cloud_callbacks_t cloud_callbacks = {
        .on_question_transcribed = cloud_question_transcribed,
        .on_answer_ready         = cloud_answer_ready,
        .on_speech_ready         = cloud_speech_ready,
        .on_failed               = cloud_failed,
        .on_request_finished     = cloud_request_finished,
    };
    ESP_RETURN_ON_ERROR(voice_cloud_brain_start(&cloud_callbacks), TAG, "cloud brain failed");

    ESP_RETURN_ON_ERROR(voice_wakeword_start(speech_event_handler), TAG, "speech engine failed");

    ESP_LOGI(TAG, "Assistant ready — say the wake word");
    return ESP_OK;
}
