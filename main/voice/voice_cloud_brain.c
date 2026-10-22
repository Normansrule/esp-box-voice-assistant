/*
 * voice_cloud_brain.c — Transcribe, answer, speak.
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "esp_check.h"
#include "esp_log.h"

#include "OpenAI.h"

#include "buddy_api_key.h"
#include "buddy_config.h"
#include "buddy_settings.h"
#include "net_time.h"
#include "net_wifi.h"
#include "voice_cloud_brain.h"
#include "voice_local_brain.h"
#include "voice_mood_tag.h"

static const char *TAG = "cloud_brain";

/**
 * The conversation is restarted after this many exchanges. Keeping some history
 * lets the buddy follow up ("what about tomorrow?"), but keeping all of it would
 * grow without bound and eventually run the heap dry.
 */
#define CONVERSATION_TURNS_BEFORE_RESET     8

/**
 * The mood instruction bolted onto the user's personality prompt. It is added
 * in code rather than stored in the settings so it cannot be edited away by
 * accident, which would leave every cloud answer wearing the same face.
 */
#define MOOD_TAG_INSTRUCTION \
    " Always begin your reply with a mood in square brackets, chosen from: " \
    "[happy] [excited] [curious] [surprised] [sad] [annoyed] [sleepy] [love] " \
    "[confused] [neutral]. Then answer in at most two sentences and under 200 " \
    "characters — it is being read aloud by a palm-sized robot and shown on a " \
    "screen the size of a matchbox, so anything longer gets cut short."

/* ------------------------------------------------------------------------ */
/* State                                                                     */
/* ------------------------------------------------------------------------ */

/** The two kinds of work the cloud brain does. */
typedef enum {
    CLOUD_REQUEST_ANSWER_QUESTION = 0,  /*!< Transcribe, answer, then speak    */
    CLOUD_REQUEST_SPEAK_TEXT,           /*!< Speak text we already have        */
} cloud_request_kind_t;

/** One queued request. */
typedef struct {
    cloud_request_kind_t kind;

    /* For CLOUD_REQUEST_ANSWER_QUESTION: audio the recorder still owns. */
    const uint8_t *wav_data;
    size_t         wav_length;

    /* For CLOUD_REQUEST_SPEAK_TEXT: a copy of the words to say. */
    char           text[BUDDY_REPLY_TEXT_MAX_LEN];
} cloud_request_t;

static QueueHandle_t s_request_queue;
static voice_cloud_callbacks_t s_callbacks;
static volatile bool s_busy;
static bool s_started;

/* The OpenAI client objects, created lazily and then reused. */
static OpenAI_t                    *s_client;
static OpenAI_AudioTranscription_t *s_transcriber;
static OpenAI_ChatCompletion_t     *s_chat;
static OpenAI_AudioSpeech_t        *s_speech;
static uint32_t                     s_turns_since_reset;

/** The audio of the last spoken reply, kept alive while it plays. */
static OpenAI_SpeechResponse_t *s_last_speech;

/* ------------------------------------------------------------------------ */
/* Availability                                                              */
/* ------------------------------------------------------------------------ */

bool voice_cloud_brain_is_available(void)
{
    return buddy_settings_has_cloud_credentials() && net_wifi_is_connected();
}

const char *voice_cloud_brain_explain_unavailability(void)
{
    const buddy_settings_t *settings = buddy_settings_get();

    if (!settings->cloud_brain_enabled) {
        return "my online brain is switched off";
    }
    if (settings->cloud_api_key[0] == '\0') {
        return "I have no API key yet";
    }
    if (!net_wifi_is_connected()) {
        return "I am not on Wi-Fi";
    }
    return NULL;
}

/* ------------------------------------------------------------------------ */
/* Building the client                                                       */
/* ------------------------------------------------------------------------ */

/**
 * @brief Throw away the client objects so the next request rebuilds them.
 *
 * Used when the conversation gets long, and when the user changes their key.
 */
static void destroy_client(void)
{
    if (s_client != NULL) {
        OpenAIDelete(s_client);
        s_client = NULL;
    }
    s_transcriber = NULL;
    s_chat = NULL;
    s_speech = NULL;
    s_turns_since_reset = 0;
}

/**
 * @brief Create the client and configure the three services on first use.
 */
static esp_err_t ensure_client(void)
{
    if (s_client != NULL) {
        return ESP_OK;
    }

    const buddy_settings_t *settings = buddy_settings_get();

    /*
     * Say out loud how long the stored key is, every time the client is built.
     *
     * This one line would have saved a great deal of trouble. A key truncated
     * by a too-small buffer is indistinguishable from a wrong key at every
     * later step — the service just says "incorrect API key" either way — but
     * it is instantly obvious here, because a project key that reads as 127
     * characters is a project key with its tail missing.
     *
     * The description never contains the secret; see buddy_api_key.c.
     */
    char key_description[48];
    buddy_api_key_describe(settings->cloud_api_key, key_description, sizeof(key_description));
    ESP_LOGI(TAG, "Building the client with key %s", key_description);

    s_client = OpenAICreate(settings->cloud_api_key);
    ESP_RETURN_ON_FALSE(s_client != NULL, ESP_ERR_NO_MEM, TAG, "could not create the client");

    OpenAIChangeBaseURL(s_client, settings->cloud_base_url);

    /* --- Speech to text ---------------------------------------------- */
    s_transcriber = s_client->audioTranscriptionCreate(s_client);
    ESP_RETURN_ON_FALSE(s_transcriber != NULL, ESP_ERR_NO_MEM, TAG, "no transcriber");
    s_transcriber->setResponseFormat(s_transcriber, OPENAI_AUDIO_RESPONSE_FORMAT_JSON);
    s_transcriber->setLanguage(s_transcriber,
                               (settings->language == BUDDY_LANGUAGE_CHINESE) ? "zh" : "en");
    s_transcriber->setTemperature(s_transcriber, 0.2f);

    /* --- The conversation --------------------------------------------- */
    s_chat = s_client->chatCreate(s_client);
    ESP_RETURN_ON_FALSE(s_chat != NULL, ESP_ERR_NO_MEM, TAG, "no chat");

    /*
     * The personality the user typed in Settings, plus the mood instruction the
     * face depends on. Together they are the buddy's whole character.
     */
    char system_prompt[BUDDY_PERSONALITY_MAX_LEN + sizeof(MOOD_TAG_INSTRUCTION) + 8];
    snprintf(system_prompt, sizeof(system_prompt), "%s%s",
             settings->personality_prompt, MOOD_TAG_INSTRUCTION);

    s_chat->setModel(s_chat, BUDDY_CLOUD_CHAT_MODEL);
    s_chat->setSystem(s_chat, system_prompt);
    s_chat->setMaxTokens(s_chat, BUDDY_CLOUD_MAX_REPLY_TOKENS);
    s_chat->setTemperature(s_chat, 0.7f);
    s_chat->setPresencePenalty(s_chat, 0.0f);
    s_chat->setFrequencyPenalty(s_chat, 0.0f);
    s_chat->setUser(s_chat, "esp-box-desk-buddy");

    /* --- Text to speech ------------------------------------------------ */
    s_speech = s_client->audioSpeechCreate(s_client);
    ESP_RETURN_ON_FALSE(s_speech != NULL, ESP_ERR_NO_MEM, TAG, "no speech synthesiser");
    s_speech->setModel(s_speech, BUDDY_CLOUD_SPEECH_MODEL);
    s_speech->setVoice(s_speech, BUDDY_CLOUD_SPEECH_VOICE);
    s_speech->setResponseFormat(s_speech, OPENAI_AUDIO_OUTPUT_FORMAT_MP3);
    s_speech->setSpeed(s_speech, 1.0f);

    s_turns_since_reset = 0;
    ESP_LOGI(TAG, "Cloud client ready (%s)", settings->cloud_base_url);
    return ESP_OK;
}

/* ------------------------------------------------------------------------ */
/* Handling one request                                                      */
/* ------------------------------------------------------------------------ */

static void report_failure(const char *reason)
{
    ESP_LOGW(TAG, "Request failed: %s", reason);
    if (s_callbacks.on_failed != NULL) {
        s_callbacks.on_failed(reason);
    }
}

/**
 * @brief Does this look like an error object rather than an answer?
 *
 * The OpenAI component hands back whatever the service sent in the same slot
 * whether the request worked or not, so a failed request arrives as a perfectly
 * valid string containing JSON like
 *
 *     {"error":{"message":"Incorrect API key provided: sk-proj-...","code":...}}
 *
 * Without this check that JSON gets treated as the answer: printed in the
 * speech bubble and read out loud, which is a memorably strange way to learn
 * that your key is wrong.
 */
static bool looks_like_an_api_error(const char *text)
{
    if (text == NULL) {
        return false;
    }
    return (strstr(text, "\"error\"") != NULL) ||
           (strstr(text, "\"invalid_request_error\"") != NULL);
}

/**
 * @brief Turn the service's error JSON into one line worth logging.
 *
 * Logs at ERROR so it stands out in the monitor, and truncates, because these
 * can run long and the interesting part — the message — comes first.
 */
static void log_api_error(const char *step, const char *body)
{
    ESP_LOGE(TAG, "%s was refused by the service. Its exact words:", step);
    ESP_LOGE(TAG, "  %.240s", (body != NULL) ? body : "(nothing)");

    /*
     * The three failures that account for almost everything, each with the fix
     * rather than the diagnosis. A log line that says what to do next is worth
     * a great deal more than one that says what went wrong.
     */
    if (body != NULL && strstr(body, "Incorrect API key") != NULL) {
        ESP_LOGE(TAG, "  -> The key is wrong or incomplete. Check its length in the "
                      "self test: a project key should be about 165 characters.");
    } else if (body != NULL && strstr(body, "quota") != NULL) {
        ESP_LOGE(TAG, "  -> The key is valid but the account has no credit. Add a "
                      "payment method at platform.openai.com/settings/organization/billing");
    } else if (body != NULL && strstr(body, "model") != NULL) {
        ESP_LOGE(TAG, "  -> This key cannot reach %s. A project-scoped key has to have "
                      "the model enabled for its project.", BUDDY_CLOUD_CHAT_MODEL);
    }
}

/**
 * @brief Wait for the network clock, because TLS cannot work without it.
 *
 * The chip has no battery-backed clock. It boots believing it is January 1970,
 * and every certificate on the internet was issued after that — so mbedTLS
 * rejects api.openai.com's certificate as "not yet valid" and the handshake
 * fails before a single byte of the request is sent.
 *
 * The clock is normally set within a second or two of the Wi-Fi coming up, so
 * this returns immediately in the ordinary case. It only earns its keep for a
 * question asked in the first few seconds after a reboot.
 *
 * @return true when the clock is believable.
 */
static bool wait_for_a_believable_clock(void)
{
    if (net_time_is_synchronised()) {
        return true;
    }

    ESP_LOGI(TAG, "Waiting for the clock before opening a secure connection");

    for (int second = 0; second < BUDDY_CLOCK_WAIT_FOR_TLS_S; second++) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        if (net_time_is_synchronised()) {
            ESP_LOGI(TAG, "Clock arrived after %d s", second + 1);
            return true;
        }
    }

    ESP_LOGE(TAG, "The clock is still unset, so certificate checks cannot pass.");
    ESP_LOGE(TAG, "  -> Something on the network is blocking NTP (UDP port 123).");
    return false;
}

/**
 * @brief Ask the service to read @p text aloud and hand the audio to the caller.
 *
 * Shared by both request kinds, since "say this" is the last step of answering
 * a question and the whole of a plain speak request.
 */
static void synthesise_and_report(const char *text)
{
    if (text == NULL || text[0] == '\0') {
        return;
    }

    s_last_speech = s_speech->speech(s_speech, (char *)text);
    if (s_last_speech == NULL) {
        ESP_LOGW(TAG, "Text to speech failed; the words are on screen only");
        return;
    }

    if (s_callbacks.on_speech_ready != NULL) {
        s_callbacks.on_speech_ready(s_last_speech->getData(s_last_speech),
                                    s_last_speech->getLen(s_last_speech));
    }
}

/**
 * @brief Free the audio of the previous reply, if any is still held.
 */
static void release_previous_speech(void)
{
    if (s_last_speech != NULL) {
        s_last_speech->deleteResponse(s_last_speech);
        s_last_speech = NULL;
    }
}

/**
 * @brief Run one question all the way through the three services.
 */
static void process_question(const cloud_request_t *request)
{
    char *transcription = NULL;
    OpenAI_StringResponse_t *answer_response = NULL;

    /*
     * Release the previous reply's audio now, not when it finished playing.
     * By the time a new request arrives the old answer is long done, and doing
     * it here means there is exactly one place that frees it.
     */
    release_previous_speech();

    /*
     * Before anything else: is the clock set? Everything below this line goes
     * over HTTPS, and HTTPS with a 1970 clock fails in a way that reads like a
     * network fault rather than a time fault.
     */
    if (!wait_for_a_believable_clock()) {
        report_failure("my clock is not set, so I cannot open a secure connection");
        return;
    }

    if (ensure_client() != ESP_OK) {
        report_failure("I could not start my online brain");
        return;
    }

    /* --- Step one: what did they say? ---------------------------------- */

    ESP_LOGI(TAG, "Step 1/3: transcribing %u bytes of audio", (unsigned)request->wav_length);

    transcription = s_transcriber->file(s_transcriber,
                                        (uint8_t *)request->wav_data,
                                        request->wav_length,
                                        OPENAI_AUDIO_INPUT_FORMAT_WAV);

    if (transcription == NULL || transcription[0] == '\0') {
        /*
         * Nothing came back at all, which is a transport failure rather than a
         * rejection — no reply to read. The two usual causes are worth naming.
         */
        ESP_LOGE(TAG, "No reply from %s at all.", buddy_settings_get()->cloud_base_url);
        ESP_LOGE(TAG, "  -> Either the TLS handshake failed, or there is no route "
                      "out. Check the Wi-Fi line in the self test.");
        report_failure("I could not reach my online brain");
        goto cleanup;
    }

    /*
     * The component hands API errors back as plain text in the same slot as a
     * successful transcription, so a JSON error object is the tell.
     */
    if (looks_like_an_api_error(transcription)) {
        log_api_error("Transcription", transcription);
        report_failure("my online brain refused the request — check the API key");
        goto cleanup;
    }

    ESP_LOGI(TAG, "Heard: %s", transcription);
    if (s_callbacks.on_question_transcribed != NULL) {
        s_callbacks.on_question_transcribed(transcription);
    }

    /* --- Step one and a half: is this something we can answer ourselves? */

    /*
     * Sums do not need a language model. Answering them here skips the chat
     * request entirely: the reply is instant instead of a second or two, and it
     * costs nothing. Everything else carries on to the model as usual.
     */
    {
        voice_reply_t local_reply;
        if (voice_local_brain_try_math(transcription, &local_reply)) {
            if (s_callbacks.on_answer_ready != NULL) {
                s_callbacks.on_answer_ready(local_reply.text, local_reply.emotion);
            }
            synthesise_and_report(local_reply.text);
            goto cleanup;
        }
    }

    /* --- Step two: what is the answer? --------------------------------- */

    ESP_LOGI(TAG, "Step 2/3: asking %s", BUDDY_CLOUD_CHAT_MODEL);

    answer_response = s_chat->message(s_chat, transcription, true /* keep history */);
    if (answer_response == NULL) {
        report_failure("my online brain did not reply");
        goto cleanup;
    }

    /*
     * Ask how many answers there are before asking for the first one.
     *
     * getData(response, 0) indexes into the response's array of choices, and a
     * refused request has no choices at all — the error text lives in the
     * response's message instead. Indexing anyway is reading past the end of an
     * empty array, which on a good day is NULL and on a bad day is a crash
     * minutes into a conversation.
     */
    if (answer_response->getLen(answer_response) == 0) {
        const char *error_text = answer_response->getError(answer_response);
        log_api_error("The question", error_text);
        report_failure("my online brain refused the question — check the API key");
        goto cleanup;
    }

    char *raw_answer = answer_response->getData(answer_response, 0);
    if (raw_answer == NULL || raw_answer[0] == '\0') {
        report_failure("my online brain sent back nothing");
        goto cleanup;
    }

    /*
     * And belt and braces: some versions of the component pass an error body
     * through as if it were the answer. Better to say the key is wrong than to
     * read a JSON error object aloud in a cheerful voice.
     */
    if (looks_like_an_api_error(raw_answer)) {
        log_api_error("The question", raw_answer);
        report_failure("my online brain refused the question — check the API key");
        goto cleanup;
    }

    face_emotion_t emotion;
    const char *answer_text = voice_mood_tag_split(raw_answer, &emotion);

    ESP_LOGI(TAG, "Answer (%s): %s", face_emotion_get_name(emotion), answer_text);
    if (s_callbacks.on_answer_ready != NULL) {
        s_callbacks.on_answer_ready(answer_text, emotion);
    }

    /* --- Step three: say it out loud ----------------------------------- */

    ESP_LOGI(TAG, "Step 3/3: turning the answer into speech");
    synthesise_and_report(answer_text);

    /* Keep the conversation bounded. */
    s_turns_since_reset++;
    if (s_turns_since_reset >= CONVERSATION_TURNS_BEFORE_RESET) {
        ESP_LOGI(TAG, "Starting a fresh conversation to keep memory in check");
        destroy_client();
    }

cleanup:
    if (answer_response != NULL) {
        answer_response->deleteResponse(answer_response);
    }
    if (transcription != NULL) {
        free(transcription);
    }
}

/**
 * @brief Speak a line the buddy already worked out for itself.
 */
static void process_speak(const cloud_request_t *request)
{
    release_previous_speech();

    if (ensure_client() != ESP_OK) {
        report_failure("I could not start my online voice");
        return;
    }

    ESP_LOGI(TAG, "Speaking: %s", request->text);
    synthesise_and_report(request->text);
}

/**
 * @brief Waits for work and does it, one item at a time.
 */
static void cloud_brain_task(void *argument)
{
    (void)argument;

    cloud_request_t request;
    while (true) {
        if (xQueueReceive(s_request_queue, &request, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        s_busy = true;
        if (request.kind == CLOUD_REQUEST_SPEAK_TEXT) {
            process_speak(&request);
        } else {
            process_question(&request);
        }

        /*
         * s_last_speech is cleared at the start of every request and only set
         * again when synthesis succeeded, so it is an exact answer to "is the
         * speaker about to be busy?".
         */
        const bool audio_will_play = (s_last_speech != NULL);

        s_busy = false;

        if (s_callbacks.on_request_finished != NULL) {
            s_callbacks.on_request_finished(audio_will_play);
        }
    }
}

/* ------------------------------------------------------------------------ */
/* Public API                                                                */
/* ------------------------------------------------------------------------ */

esp_err_t voice_cloud_brain_start(const voice_cloud_callbacks_t *callbacks)
{
    ESP_RETURN_ON_FALSE(callbacks != NULL, ESP_ERR_INVALID_ARG, TAG, "callbacks required");
    if (s_started) {
        return ESP_OK;
    }

    s_callbacks = *callbacks;

    s_request_queue = xQueueCreate(1, sizeof(cloud_request_t));
    ESP_RETURN_ON_FALSE(s_request_queue != NULL, ESP_ERR_NO_MEM, TAG, "no memory for the queue");

    /*
     * A generous stack: TLS handshakes and JSON parsing are both hungry, and a
     * stack overflow here would look like a random crash minutes into a chat.
     */
    const BaseType_t created = xTaskCreatePinnedToCore(cloud_brain_task, "cloud_brain",
                                                       8 * 1024, NULL, 4, NULL, 0);
    ESP_RETURN_ON_FALSE(created == pdPASS, ESP_FAIL, TAG, "task failed to start");

    s_started = true;
    ESP_LOGI(TAG, "Cloud brain standing by");
    return ESP_OK;
}

esp_err_t voice_cloud_brain_ask(const uint8_t *wav_data, size_t wav_length)
{
    ESP_RETURN_ON_FALSE(s_started, ESP_ERR_INVALID_STATE, TAG, "not started");
    ESP_RETURN_ON_FALSE(wav_data != NULL && wav_length > 0, ESP_ERR_INVALID_ARG, TAG, "no audio");

    if (s_busy) {
        ESP_LOGW(TAG, "Still working on the last question");
        return ESP_ERR_INVALID_STATE;
    }

    cloud_request_t request = {
        .kind = CLOUD_REQUEST_ANSWER_QUESTION,
        .wav_data = wav_data,
        .wav_length = wav_length,
    };

    return (xQueueSend(s_request_queue, &request, 0) == pdTRUE) ? ESP_OK : ESP_ERR_TIMEOUT;
}

esp_err_t voice_cloud_brain_speak(const char *text)
{
    ESP_RETURN_ON_FALSE(s_started, ESP_ERR_INVALID_STATE, TAG, "not started");
    ESP_RETURN_ON_FALSE(text != NULL && text[0] != '\0', ESP_ERR_INVALID_ARG, TAG, "no text");

    if (s_busy) {
        ESP_LOGW(TAG, "Still working, cannot speak right now");
        return ESP_ERR_INVALID_STATE;
    }

    /*
     * Decline while the clock is unset, rather than queueing and failing later.
     *
     * This path only ever speaks a line the buddy already worked out for
     * itself, and there is a recorded clip of most of those sitting on flash.
     * Refusing HERE — synchronously, before the request is queued — is what
     * lets voice_assistant_deliver_reply() fall straight through to the offline
     * voice, so the buddy answers immediately in its recorded voice instead of
     * pausing and then apologising.
     *
     * The question path in voice_cloud_brain_ask() has no such fallback, so it
     * waits for the clock instead of giving way.
     */
    if (!net_time_is_synchronised()) {
        ESP_LOGI(TAG, "Clock not set yet — leaving this line to the offline voice");
        return ESP_ERR_INVALID_STATE;
    }

    cloud_request_t request = { .kind = CLOUD_REQUEST_SPEAK_TEXT };
    strncpy(request.text, text, sizeof(request.text) - 1);
    request.text[sizeof(request.text) - 1] = '\0';

    return (xQueueSend(s_request_queue, &request, 0) == pdTRUE) ? ESP_OK : ESP_ERR_TIMEOUT;
}

bool voice_cloud_brain_is_busy(void)
{
    return s_busy;
}

void voice_cloud_brain_reset_conversation(void)
{
    if (!s_busy) {
        destroy_client();
    }
}
