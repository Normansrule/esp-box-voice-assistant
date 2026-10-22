/*
 * voice_wakeword.h — The always-listening speech front end.
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 *
 * This module owns the two neural networks and the two tasks that keep them
 * fed. It reports what it hears through a single callback and knows nothing
 * about what the buddy does with that information.
 *
 * Two FreeRTOS tasks run continuously once started:
 *
 *   feed task     pulls microphone frames and pushes them into the audio front
 *                 end (noise suppression, beam forming, gain control)
 *   detect task   pulls cleaned frames back out, runs the wake word detector
 *                 on them, and once woken runs the phrase recogniser
 *
 * They are pinned to opposite cores so a busy network stack cannot make the
 * buddy deaf.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "voice_commands.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief The things the speech front end can report.
 */
typedef enum {
    VOICE_EVENT_WAKE_WORD,        /*!< "Hi E.S.P." heard; start listening      */
    VOICE_EVENT_COMMAND_MATCHED,  /*!< One of the offline phrases was said     */
    VOICE_EVENT_LISTEN_TIMEOUT,   /*!< The listening window closed unmatched   */
} voice_event_type_t;

/**
 * @brief Details of one report from the speech front end.
 */
typedef struct {
    voice_event_type_t type;
    voice_action_t     action;    /*!< Valid for VOICE_EVENT_COMMAND_MATCHED   */
    const char        *phrase;    /*!< The phrase text, for display            */
} voice_event_t;

/**
 * @brief Handler for speech events.
 *
 * @warning Called from the detect task. Do not block for long, and take the
 *          display lock before touching any LVGL object.
 */
typedef void (*voice_event_handler_t)(const voice_event_t *event);

/**
 * @brief Load the models and start listening.
 *
 * @param handler  Called for every event. Must not be NULL.
 */
esp_err_t voice_wakeword_start(voice_event_handler_t handler);

/**
 * @brief Reload the models for a different language and re-register the phrases.
 *
 * Takes a second or two; the buddy is deaf while it happens.
 */
esp_err_t voice_wakeword_set_language(buddy_language_t language);

/**
 * @brief Reasons the buddy might need to stop listening for a moment.
 *
 * WHY THESE ARE NAMED RATHER THAN A BOOL
 * --------------------------------------
 * This used to be a single flag with set_paused(true/false), and that flag was
 * the cause of the worst bug in the firmware: the buddy going permanently deaf.
 *
 * The trouble is that several things need the microphone quiet AT THE SAME
 * TIME, and they finish in a different order than they started. A cloud request
 * holds it quiet for a few seconds; a kitchen timer going off in the middle of
 * that plays a chime, which also needs it quiet — and when the chime finished
 * it set the flag back to false, releasing a hold it did not own. The
 * microphone came back mid-request, the wake word fired, and the recording
 * being uploaded was overwritten underneath the upload.
 *
 * With named holds, releasing one says only "I am finished", and the buddy
 * stays deaf until the last holder lets go. Releasing a hold you never took
 * does nothing, and a leak can be read straight out of the log because the hold
 * still set is the one that names the culprit.
 */
typedef enum {
    VOICE_HOLD_CHIME    = 1u << 0,  /*!< A notification sound is playing      */
    VOICE_HOLD_SPEAKING = 1u << 1,  /*!< The buddy is saying something        */
    VOICE_HOLD_THINKING = 1u << 2,  /*!< A cloud request is in flight         */
    VOICE_HOLD_MUSIC    = 1u << 3,  /*!< A track is playing through the same speaker */
} voice_hold_t;

/**
 * @brief Stop feeding audio to the models, for a named reason.
 *
 * Taking a hold twice is the same as taking it once.
 */
void voice_wakeword_hold(voice_hold_t reason);

/**
 * @brief Let go of one reason. Listening resumes when none are left.
 *
 * Safe to call for a hold that was never taken.
 */
void voice_wakeword_release(voice_hold_t reason);

/**
 * @brief Drop every hold at once.
 *
 * The recovery path, not a normal one. voice_assistant.c's watchdog calls it
 * when holds have outlived everything that could justify them, so that a bug
 * anywhere in the chain costs a couple of seconds rather than the rest of the
 * session.
 */
void voice_wakeword_release_all(void);

/**
 * @brief Which holds are currently set, as a bitmask of voice_hold_t.
 *
 * Zero means the buddy is listening.
 */
uint32_t voice_wakeword_get_holds(void);

/**
 * @brief Tune listening for the kind of room this box lives in.
 *
 * Sets two things together, because they are two halves of one decision: how
 * far above the room's own background a sound has to be before it counts as
 * speech, and how sure the phrase recogniser has to be before its best guess
 * is acted on. In a noisy room both should be higher — there is more for the
 * recogniser to mishear, and more background for the gate to mistake for
 * somebody talking.
 *
 * Safe to call at any time, including while listening.
 */
void voice_wakeword_apply_noise_profile(buddy_noise_profile_t profile);

/**
 * @brief The confidence a phrase match currently needs, after the profile.
 *
 * Reported in the self test, so the setting can be seen rather than assumed.
 */
float voice_wakeword_get_confidence_floor(void);

/**
 * @brief True when the phrase recogniser is in its listening window.
 */
bool voice_wakeword_is_listening(void);

/**
 * @brief True once the models are loaded and the audio tasks are running.
 */
bool voice_wakeword_is_running(void);

/**
 * @brief How many phrases were successfully registered with the recogniser.
 *
 * Zero means the vocabulary failed to load, which looks identical from the
 * outside to the buddy simply not understanding anything. Used by the self test.
 */
uint16_t voice_wakeword_get_phrase_count(void);

/**
 * @brief Abandon the current listening window and go back to waiting for the
 *        wake word.
 */
void voice_wakeword_cancel_listening(void);

#ifdef __cplusplus
}
#endif
