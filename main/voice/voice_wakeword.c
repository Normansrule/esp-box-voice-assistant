/*
 * voice_wakeword.c — WakeNet and MultiNet, wrapped up behind one callback.
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 *
 * The structure here follows Espressif's reference speech pipeline closely,
 * because that pipeline is carefully tuned and easy to get subtly wrong. What
 * has changed is the shape of it: the recognition machinery is separated from
 * what the buddy does about it, so this file never mentions faces or music.
 */

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"

#include "esp_afe_sr_iface.h"
#include "esp_afe_sr_models.h"
#include "esp_mn_iface.h"
#include "esp_mn_models.h"
#include "esp_mn_speech_commands.h"
#include "esp_process_sdkconfig.h"
#include "esp_wn_iface.h"
#include "esp_wn_models.h"
#include "model_path.h"

#include "buddy_config.h"
#include "buddy_settings.h"
#include "hw_microphone.h"
#include "voice_recorder.h"
#include "voice_speech_gate.h"
#include "voice_wakeword.h"

static const char *TAG = "wakeword";

/** Depth of the queue between the detect task and the handler task. */
#define EVENT_QUEUE_DEPTH           4

/**
 * Upper bound on how many phrases we hand to MultiNet in one language.
 *
 * Lives in buddy_config.h rather than here so the host tests can check the
 * vocabulary against it. Going over it drops the extra phrases with a warning
 * that is easy to miss in a boot log; a failing test is not.
 */
#define MAX_REGISTERED_PHRASES      BUDDY_MAX_REGISTERED_PHRASES

/* ------------------------------------------------------------------------ */
/* State                                                                     */
/* ------------------------------------------------------------------------ */

/** The catalogue of speech models flashed into the "model" partition. */
static srmodel_list_t *s_models;

/** The audio front end: noise suppression, gain control, wake word. */
static esp_afe_sr_iface_t   *s_afe_interface;
static esp_afe_sr_data_t    *s_afe_data;

/** The phrase recogniser that runs after the wake word. */
static const esp_mn_iface_t *s_multinet;
static model_iface_data_t   *s_multinet_data;
static char                 *s_multinet_name;

/** Buffer the feed task reads microphone frames into (internal RAM, DMA-able). */
static int16_t *s_feed_buffer;

/**
 * MultiNet identifies phrases by a number we choose when registering them.
 * These arrays turn that number back into something meaningful.
 */
static voice_action_t s_phrase_actions[MAX_REGISTERED_PHRASES];
static const char    *s_phrase_texts[MAX_REGISTERED_PHRASES];
static uint16_t       s_phrase_count;

static QueueHandle_t          s_event_queue;
static voice_event_handler_t  s_event_handler;
static buddy_language_t       s_language = BUDDY_LANGUAGE_COUNT;  /* Unset */

/**
 * Which reasons are currently keeping the microphone quiet.
 *
 * Zero means listening. See voice_hold_t in the header for why this is a set of
 * named reasons rather than the bool it used to be.
 */
static volatile uint32_t s_holds;

static volatile bool s_listening;

/**
 * How sure a phrase match has to be, after the noise profile has had its say.
 *
 * A variable rather than the bare constant, because the right value depends on
 * the room: in a quiet study almost any match is worth acting on, and in a
 * kitchen the recogniser's best guess is much more often a guess.
 */
static float s_confidence_floor = BUDDY_COMMAND_MIN_CONFIDENCE;
static volatile bool s_cancel_requested;
static bool s_started;

/* ------------------------------------------------------------------------ */
/* Publishing events                                                         */
/* ------------------------------------------------------------------------ */

static void publish_event(voice_event_type_t type, voice_action_t action, const char *phrase)
{
    const voice_event_t event = {
        .type = type,
        .action = action,
        .phrase = phrase,
    };

    if (s_event_queue != NULL) {
        /* Never block the detect task: a dropped event beats a deaf buddy. */
        if (xQueueSend(s_event_queue, &event, 0) != pdTRUE) {
            ESP_LOGW(TAG, "Event queue full, dropped event %d", (int)type);
        }
    }
}

/**
 * @brief Drains the event queue and calls the application's handler.
 *
 * Running the handler on its own task means a slow reaction — a cloud round
 * trip, say — cannot stall speech recognition.
 */
static void event_handler_task(void *argument)
{
    (void)argument;

    voice_event_t event;
    while (true) {
        if (xQueueReceive(s_event_queue, &event, portMAX_DELAY) == pdTRUE) {
            if (s_event_handler != NULL) {
                s_event_handler(&event);
            }
        }
    }
}

/* ------------------------------------------------------------------------ */
/* The two audio tasks                                                       */
/* ------------------------------------------------------------------------ */

/**
 * @brief Reads the microphones and pushes frames into the audio front end.
 */
static void audio_feed_task(void *argument)
{
    esp_afe_sr_data_t *afe_data = (esp_afe_sr_data_t *)argument;
    const int frame_samples = s_afe_interface->get_feed_chunksize(afe_data);

    /*
     * Three channels out: two microphones plus an echo reference slot. This
     * buffer has to live in internal RAM because it is touched every few
     * milliseconds and PSRAM latency would show up as dropped frames.
     */
    s_feed_buffer = heap_caps_malloc(frame_samples * 3 * sizeof(int16_t),
                                     MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (s_feed_buffer == NULL) {
        ESP_LOGE(TAG, "No internal RAM for the microphone frame buffer");
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "Feeding %d samples per frame", frame_samples);

    while (true) {
        if (s_holds != 0) {
            /*
             * The buddy is talking or chiming. Stop feeding so it cannot hear
             * itself, wake on its own voice, or transcribe its own reply.
             */
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }

        if (hw_microphone_read_frame(s_feed_buffer, (size_t)frame_samples) != ESP_OK) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        s_afe_interface->feed(afe_data, s_feed_buffer);
    }
}

/**
 * @brief Pulls cleaned frames back out and runs the two recognisers on them.
 */
static void audio_detect_task(void *argument)
{
    esp_afe_sr_data_t *afe_data = (esp_afe_sr_data_t *)argument;
    const int frame_samples = s_afe_interface->get_fetch_chunksize(afe_data);

    /*
     * The phrase recogniser must be fed exactly the frame size it was built
     * for. A mismatch means the models were configured inconsistently, and it
     * shows up as recognition that simply never works — much easier to spot
     * here than to debug later.
     */
    const int expected_samples = s_multinet->get_samp_chunksize(s_multinet_data);
    if (expected_samples != frame_samples) {
        ESP_LOGE(TAG, "Frame size mismatch: front end gives %d, command model wants %d",
                 frame_samples, expected_samples);
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "Listening for the wake word (%d samples per frame)", frame_samples);

    while (true) {
        afe_fetch_result_t *result = s_afe_interface->fetch(afe_data);
        if (result == NULL || result->ret_value == ESP_FAIL) {
            continue;
        }

        /* --- Stage one: has somebody said the wake word? ------------------ */

        if (result->wakeup_state == WAKENET_DETECTED) {
            ESP_LOGI(TAG, "Wake word heard");
            s_listening = true;
            s_cancel_requested = false;
            voice_recorder_start();
            publish_event(VOICE_EVENT_WAKE_WORD, VOICE_ACTION_NONE, NULL);
            continue;
        }

        if (result->wakeup_state == WAKENET_CHANNEL_VERIFIED) {
            /*
             * The front end has worked out which microphone heard the speaker
             * best and locked onto it. Switching the wake word detector off now
             * frees the processor for phrase recognition.
             */
            ESP_LOGD(TAG, "Locked onto microphone %d", result->trigger_channel_id);
            s_afe_interface->disable_wakenet(afe_data);
            continue;
        }

        if (!s_listening) {
            /*
             * Not a conversation, so this is the room talking to itself — the
             * fan, the traffic, the television. Measuring it here is what lets
             * the speech gate ask "was that louder than this room normally is"
             * instead of comparing against a number picked on somebody else's
             * desk. It is the same post-front-end audio the recorder sees, so
             * the two measurements are directly comparable.
             */
            voice_speech_gate_observe_room(result->data, (size_t)frame_samples);
            continue;
        }

        /* --- Stage two: what did they say? ------------------------------- */

        if (s_cancel_requested) {
            ESP_LOGD(TAG, "Listening cancelled");
            s_listening = false;
            voice_recorder_stop();
            s_afe_interface->enable_wakenet(afe_data);
            continue;
        }

        /* Keep a copy in case nothing matches and the cloud has to hear it. */
        voice_recorder_append(result->data, (size_t)frame_samples);

        const esp_mn_state_t state = s_multinet->detect(s_multinet_data, result->data);

        if (state == ESP_MN_STATE_DETECTING) {
            continue;   /* Still within the window, nothing decided yet. */
        }

        if (state == ESP_MN_STATE_TIMEOUT) {
            ESP_LOGI(TAG, "No known phrase in %.2f s of speech",
                     voice_recorder_get_duration_seconds());
            s_listening = false;
            voice_recorder_stop();
            publish_event(VOICE_EVENT_LISTEN_TIMEOUT, VOICE_ACTION_NONE, NULL);
            s_afe_interface->enable_wakenet(afe_data);
            continue;
        }

        if (state == ESP_MN_STATE_DETECTED) {
            esp_mn_results_t *matches = s_multinet->get_results(s_multinet_data);

            voice_action_t action = VOICE_ACTION_NONE;
            const char *phrase = NULL;
            bool too_unsure = false;

            if (matches != NULL && matches->num > 0) {
                const int id = matches->command_id[0];
                const float confidence = matches->prob[0];

                /*
                 * The runner-up, which is the measurement that actually
                 * separates a command from a question.
                 *
                 * MultiNet always returns its best phrase, and "best" is not
                 * "right": around ninety phrases are registered, so a real
                 * question is matched to whichever of them it sounds least
                 * unlike. Ask about the capital of France and it may come back
                 * as "what is the date".
                 *
                 * v3.6 tried to catch that with an absolute floor on prob[0],
                 * and that was a mistake — prob[0] is not a calibrated
                 * probability, and a floor of 0.35 threw away correct matches
                 * scoring 0.2, which is most of them. What DOES distinguish
                 * the two cases is the gap to the second-best phrase: a spoken
                 * command wins clearly, while a question force-fitted to the
                 * vocabulary produces a cluster of near-ties, because nothing
                 * really matched. That comparison is a ratio, so it holds
                 * whatever scale the model reports on.
                 */
                const float runner_up = (matches->num > 1) ? matches->prob[1] : 0.0f;
                const bool  ambiguous =
                    (confidence > 0.0f) &&
                    (runner_up > confidence * BUDDY_COMMAND_MARGIN_OVER_RUNNER_UP);

                if (id < 0 || id >= (int)s_phrase_count) {
                    ESP_LOGW(TAG, "Phrase id %d is outside the table", id);
                } else if (confidence < s_confidence_floor || ambiguous) {
                    /*
                     * Treating a weak or ambiguous match as no match sends the
                     * recording down the same path as speech that matched
                     * nothing — which in Offline mode still reaches the local
                     * brain, and online reaches the model. Answering the actual
                     * question beats answering a phrase nobody said.
                     */
                    ESP_LOGI(TAG, "Best guess \"%s\" (%.2f, runner-up %.2f) is %s; "
                                  "treating it as a question instead",
                             s_phrase_texts[id], confidence, runner_up,
                             ambiguous ? "too close to call" : "below the floor");
                    too_unsure = true;
                } else {
                    action = s_phrase_actions[id];
                    phrase = s_phrase_texts[id];
                    ESP_LOGI(TAG, "Recognised \"%s\" (%.2f, runner-up %.2f)",
                             phrase, confidence, runner_up);
                }
            }

            s_listening = false;
            voice_recorder_stop();
            publish_event(too_unsure ? VOICE_EVENT_LISTEN_TIMEOUT
                                     : VOICE_EVENT_COMMAND_MATCHED,
                          action, phrase);
            s_afe_interface->enable_wakenet(afe_data);
            continue;
        }
    }
}

/* ------------------------------------------------------------------------ */
/* Loading models and phrases                                                */
/* ------------------------------------------------------------------------ */

/**
 * @brief Hand every phrase for the active language over to MultiNet.
 */
static esp_err_t register_phrases(buddy_language_t language)
{
    /*
     * MultiNet 6 takes phrases as ordinary text; earlier models want Espressif's
     * phonetic spelling. The table carries both, so all that changes is which
     * column gets used.
     */
    const bool model_takes_plain_text = (s_multinet_name != NULL) &&
                                        (strstr(s_multinet_name, "mn6_en") != NULL);

    if (s_multinet_name != NULL && strstr(s_multinet_name, "mn6") != NULL) {
        esp_mn_commands_clear();
    }

    s_phrase_count = 0;

    for (uint16_t i = 0; i < voice_commands_get_count(); i++) {
        const voice_command_t *command = voice_commands_get(i);
        if (command == NULL || command->language != language) {
            continue;
        }
        if (s_phrase_count >= MAX_REGISTERED_PHRASES) {
            ESP_LOGW(TAG, "Phrase table is full; the rest were skipped");
            break;
        }

        const char *text = model_takes_plain_text ? command->phrase : command->phonetic;
        esp_mn_commands_add(s_phrase_count, (char *)text);

        s_phrase_actions[s_phrase_count] = command->action;
        s_phrase_texts[s_phrase_count] = command->phrase;
        s_phrase_count++;
    }

    /* Push the list into the model and report anything it could not use. */
    esp_mn_error_t *errors = esp_mn_commands_update(s_multinet, s_multinet_data);
    if (errors != NULL) {
        for (int i = 0; i < errors->num; i++) {
            ESP_LOGE(TAG, "MultiNet rejected phrase %d", errors->phrases[i]);
        }
    }

    ESP_LOGI(TAG, "Registered %u phrases", s_phrase_count);
    return ESP_OK;
}

esp_err_t voice_wakeword_set_language(buddy_language_t language)
{
    if (s_afe_data == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (language == s_language) {
        return ESP_OK;
    }
    if (language >= BUDDY_LANGUAGE_COUNT) {
        language = BUDDY_LANGUAGE_ENGLISH;
    }

    ESP_LOGI(TAG, "Switching to %s",
             (language == BUDDY_LANGUAGE_ENGLISH) ? "English" : "Chinese");

    /* Drop the old phrase model before loading its replacement. */
    if (s_multinet_data != NULL) {
        s_multinet->destroy(s_multinet_data);
        s_multinet_data = NULL;
    }

    /* Pick the matching wake word: "Hi E.S.P." for English, "Hi Lexin" for Chinese. */
    char *wakenet_name = esp_srmodel_filter(s_models, ESP_WN_PREFIX,
                                            (language == BUDDY_LANGUAGE_ENGLISH)
                                                ? "hiesp" : "hilexin");
    ESP_RETURN_ON_FALSE(wakenet_name != NULL, ESP_ERR_NOT_FOUND, TAG,
                        "That wake word is not in the model partition — check menuconfig");
    s_afe_interface->set_wakenet(s_afe_data, wakenet_name);
    ESP_LOGI(TAG, "Wake word model: %s", wakenet_name);

    /* Then the matching phrase recogniser. */
    s_multinet_name = esp_srmodel_filter(s_models, ESP_MN_PREFIX,
                                         (language == BUDDY_LANGUAGE_ENGLISH)
                                             ? ESP_MN_ENGLISH : ESP_MN_CHINESE);
    ESP_RETURN_ON_FALSE(s_multinet_name != NULL, ESP_ERR_NOT_FOUND, TAG,
                        "That command model is not in the model partition");

    s_multinet = esp_mn_handle_from_name(s_multinet_name);
    s_multinet_data = s_multinet->create(s_multinet_name, BUDDY_LISTEN_WINDOW_MS);
    ESP_RETURN_ON_FALSE(s_multinet_data != NULL, ESP_ERR_NO_MEM, TAG,
                        "Could not create the command model");
    ESP_LOGI(TAG, "Command model: %s", s_multinet_name);

    s_language = language;
    return register_phrases(language);
}

/* ------------------------------------------------------------------------ */
/* Lifecycle                                                                 */
/* ------------------------------------------------------------------------ */

esp_err_t voice_wakeword_start(voice_event_handler_t handler)
{
    ESP_RETURN_ON_FALSE(handler != NULL, ESP_ERR_INVALID_ARG, TAG, "handler required");
    ESP_RETURN_ON_FALSE(!s_started, ESP_ERR_INVALID_STATE, TAG, "already running");

    s_event_handler = handler;

    s_event_queue = xQueueCreate(EVENT_QUEUE_DEPTH, sizeof(voice_event_t));
    ESP_RETURN_ON_FALSE(s_event_queue != NULL, ESP_ERR_NO_MEM, TAG, "no memory for the event queue");

    /* Discover which speech models were flashed into the "model" partition. */
    s_models = esp_srmodel_init("model");
    ESP_RETURN_ON_FALSE(s_models != NULL, ESP_ERR_NOT_FOUND, TAG,
                        "No speech models found — reflash the model partition");

    /* Build the audio front end. */
    s_afe_interface = (esp_afe_sr_iface_t *)&ESP_AFE_SR_HANDLE;

    afe_config_t afe_config = AFE_CONFIG_DEFAULT();
    afe_config.wakenet_model_name = esp_srmodel_filter(s_models, ESP_WN_PREFIX, NULL);

    /*
     * Acoustic echo cancellation is switched off deliberately. It costs a lot of
     * processor time, and the buddy already stops listening while it talks, so
     * there is nothing for it to cancel.
     */
    afe_config.aec_init = false;

    s_afe_data = s_afe_interface->create_from_config(&afe_config);
    ESP_RETURN_ON_FALSE(s_afe_data != NULL, ESP_ERR_NO_MEM, TAG,
                        "Could not create the audio front end");

    /* Load the models for whichever language the user picked. */
    ESP_RETURN_ON_ERROR(voice_wakeword_set_language(buddy_settings_get()->language),
                        TAG, "language setup failed");

    /* And tune the thresholds for the kind of room this box is in. */
    voice_wakeword_apply_noise_profile(buddy_settings_get()->noise_profile);

    /*
     * Feed on core 0 alongside Wi-Fi, detect on core 1 alongside the display.
     * Splitting them keeps a busy network from causing missed wake words.
     */
    BaseType_t created;

    created = xTaskCreatePinnedToCore(audio_feed_task, "voice_feed", 4 * 1024,
                                      s_afe_data, 5, NULL, 0);
    ESP_RETURN_ON_FALSE(created == pdPASS, ESP_FAIL, TAG, "feed task failed to start");

    created = xTaskCreatePinnedToCore(audio_detect_task, "voice_detect", 8 * 1024,
                                      s_afe_data, 5, NULL, 1);
    ESP_RETURN_ON_FALSE(created == pdPASS, ESP_FAIL, TAG, "detect task failed to start");

    created = xTaskCreatePinnedToCore(event_handler_task, "voice_events", 6 * 1024,
                                      NULL, 4, NULL, 0);
    ESP_RETURN_ON_FALSE(created == pdPASS, ESP_FAIL, TAG, "event task failed to start");

    s_started = true;
    ESP_LOGI(TAG, "Speech pipeline running");
    return ESP_OK;
}

void voice_wakeword_hold(voice_hold_t reason)
{
    const uint32_t before = s_holds;
    s_holds |= (uint32_t)reason;

    if (before == 0 && s_holds != 0) {
        ESP_LOGD(TAG, "Ears closed (holds 0x%02x)", (unsigned)s_holds);
    }
}

void voice_wakeword_release(voice_hold_t reason)
{
    s_holds &= ~(uint32_t)reason;

    if (s_holds == 0) {
        ESP_LOGD(TAG, "Ears open");
    }
}

void voice_wakeword_release_all(void)
{
    if (s_holds != 0) {
        ESP_LOGW(TAG, "Forcing every listening hold open (was 0x%02x)", (unsigned)s_holds);
    }
    s_holds = 0;
}

uint32_t voice_wakeword_get_holds(void)
{
    return s_holds;
}

void voice_wakeword_apply_noise_profile(buddy_noise_profile_t profile)
{
    /*
     * One table rather than two, so the pair can never drift apart. Both
     * numbers answer the same question — how much benefit of the doubt does
     * this room deserve — and tuning one without the other produces a box that
     * hears you and then acts on the wrong phrase, or the reverse.
     *
     * The confidences here are all far lower than they were in v3.6, where
     * even the most permissive profile sat at 0.25 — above what MultiNet
     * reports on a correct match, so every profile rejected nearly every
     * command. See BUDDY_COMMAND_MIN_CONFIDENCE in buddy_config.h.
     */
    static const struct {
        uint8_t     margin;      /* Multiples of the room's own level  */
        float       confidence;  /* Phrase match floor                 */
        const char *name;
    } profiles[BUDDY_NOISE_PROFILE_COUNT] = {
        [BUDDY_NOISE_AUTOMATIC]  = { BUDDY_SPEECH_OVER_ROOM_MARGIN,     0.10f, "automatic" },
        [BUDDY_NOISE_QUIET_ROOM] = { BUDDY_SPEECH_OVER_ROOM_MARGIN - 2, 0.05f, "quiet room" },
        [BUDDY_NOISE_NOISY_ROOM] = { BUDDY_SPEECH_OVER_ROOM_MARGIN + 4, 0.20f, "noisy room" },
    };

    if (profile >= BUDDY_NOISE_PROFILE_COUNT) {
        profile = BUDDY_NOISE_AUTOMATIC;
    }

    voice_speech_gate_set_margin(profiles[profile].margin);
    s_confidence_floor = profiles[profile].confidence;

    ESP_LOGI(TAG, "Listening tuned for a %s: %ux over the room, %.2f confidence",
             profiles[profile].name, (unsigned)profiles[profile].margin,
             (double)profiles[profile].confidence);
}

float voice_wakeword_get_confidence_floor(void)
{
    return s_confidence_floor;
}

bool voice_wakeword_is_listening(void)
{
    return s_listening;
}

bool voice_wakeword_is_running(void)
{
    return s_started;
}

uint16_t voice_wakeword_get_phrase_count(void)
{
    return s_phrase_count;
}

void voice_wakeword_cancel_listening(void)
{
    if (s_listening) {
        s_cancel_requested = true;
    }
}
