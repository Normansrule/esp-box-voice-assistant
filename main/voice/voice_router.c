/*
 * voice_router.c — Deciding who answers: the chip, the cloud, or nobody.
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 *
 * Pure decision logic. See voice_router.h for the policy this implements and
 * why it lives in a file of its own.
 */

#include "buddy_config.h"
#include "voice_router.h"

/** One hour, in microseconds. The window the allowance is measured over. */
#define BUDGET_WINDOW_US        (60LL * 60LL * 1000000LL)

/* ------------------------------------------------------------------------ */
/* The decision                                                              */
/* ------------------------------------------------------------------------ */

voice_route_t voice_router_decide(const voice_route_inputs_t *inputs, const char **reason)
{
    if (reason != NULL) {
        *reason = NULL;
    }
    if (inputs == NULL) {
        return VOICE_ROUTE_NOTHING_HEARD;
    }

    /*
     * A confident match is answered on the chip, in every mode.
     *
     * Even in ALWAYS ASK: paying a service to tell you the time when the box
     * has a clock, or to pull a face when the box has a face, is worse than
     * pointless — it is slower. The cloud is for the questions the chip cannot
     * answer, not for the ones it can.
     */
    if (inputs->phrase_matched && inputs->phrase_confidence >= BUDDY_COMMAND_MIN_CONFIDENCE) {
        return VOICE_ROUTE_ANSWER_OFFLINE;
    }

    /*
     * Was there anything at all? — and note how little this asks.
     *
     * THIS TEST USED TO BE THE SPEECH GATE, AND THAT WAS THE v3.6 BUG.
     * The gate measures how much of the recording stood out from the room, and
     * it exists for one purpose: not paying to transcribe a cough. Putting it
     * here, above everything else, meant a cost-control device decided whether
     * the buddy responded AT ALL — so a gate mis-tuned by a loud room silenced
     * answers the chip would have given for free. The user got "I did not hear
     * anything" to every question, on a box with a working microphone.
     *
     * So the only thing that may silence the buddy now is evidence that the
     * microphone produced nothing: a recording too short to hold a sentence, or
     * one whose loudest single sample never rose to an audible level. Neither
     * depends on the room estimate, the noise profile, or any tuned threshold
     * that can drift. Everything else gets an answer of some kind.
     */
    const bool long_enough = inputs->recorded_seconds >= BUDDY_MIN_QUESTION_SECONDS;
    const bool audible     = inputs->peak_amplitude >= BUDDY_AUDIBLE_PEAK_AMPLITUDE;

    if (!long_enough || !audible) {
        return VOICE_ROUTE_NOTHING_HEARD;
    }

    /* From here on there IS a question, and the only issue is who answers it. */

    if (inputs->mode == BUDDY_BRAIN_OFFLINE_ONLY) {
        /*
         * Not a decline with an explanation — the buddy is doing exactly what
         * it was told to do, and "I do not know that one" is the honest answer
         * rather than a complaint about a feature nobody asked for.
         *
         * Reached WITHOUT consulting the speech gate, on purpose. An offline
         * answer costs nothing, so there is nothing for a gate to protect.
         */
        return VOICE_ROUTE_ANSWER_OFFLINE;
    }

    if (inputs->mode == BUDDY_BRAIN_HOME_AI) {
        /*
         * The home brain is a computer somebody may simply have switched off
         * for the night, so its absence is ordinary rather than a fault: answer
         * from the chip, as Smart does, instead of apologising every time.
         *
         * cloud_ready is not consulted at all. This mode never touches OpenAI,
         * so whether a key is saved there is beside the point.
         */
        if (!inputs->home_ready) {
            return VOICE_ROUTE_ANSWER_OFFLINE;
        }

        /*
         * Free, but not instant: a small model on an ordinary PC takes a few
         * seconds to hear, think and speak. So the same two filters Smart uses
         * to protect money protect time here — a cough is not a question, and
         * a weak match is a command said badly that the chip can just obey.
         * There is no hourly allowance, because there is no bill.
         */
        if (inputs->speech_seconds < BUDDY_MIN_SPEECH_SECONDS) {
            return VOICE_ROUTE_ANSWER_OFFLINE;
        }
        if (inputs->phrase_matched &&
            inputs->phrase_confidence >= BUDDY_COMMAND_NEARLY_CONFIDENCE) {
            return VOICE_ROUTE_ANSWER_OFFLINE;
        }
        return VOICE_ROUTE_ASK_HOME;
    }

    if (!inputs->cloud_ready) {
        /*
         * Smart mode falls back rather than complaining.
         *
         * Somebody in Smart mode asked for the cloud when it helps, not for a
         * spoken apology every time the Wi-Fi is down or the key is missing.
         * The offline brain answers what it can and says "I do not know that
         * one" otherwise, which is more use than an explanation of a feature
         * that is not going to come back this minute.
         *
         * Always-ask mode still declines, because there the user has said
         * plainly that the model's answer is the one they want, and quietly
         * substituting a canned reply would be misleading.
         */
        if (inputs->mode == BUDDY_BRAIN_SMART) {
            return VOICE_ROUTE_ANSWER_OFFLINE;
        }

        if (reason != NULL) {
            *reason = "I am not connected to my online brain";
        }
        return VOICE_ROUTE_DECLINE_NO_CLOUD;
    }

    /*
     * NOW the speech gate, in the one place it belongs: guarding the path that
     * spends money and several seconds. Its verdict here is "answer offline",
     * never "say nothing" — being unsure whether that was really a question is
     * a reason not to pay for it, not a reason to ignore the person.
     */
    if (inputs->speech_seconds < BUDDY_MIN_SPEECH_SECONDS) {
        return VOICE_ROUTE_ANSWER_OFFLINE;
    }

    /*
     * SMART mode asks for more than ALWAYS ASK does.
     *
     * A weak phrase match means the recogniser heard SOMETHING like a known
     * command — probably a command, said badly. Sending that to a language
     * model gets a polite paragraph about a phrase the box could have simply
     * obeyed. In SMART mode the benefit of the doubt goes to the offline
     * vocabulary; in ALWAYS ASK the user has said they would rather have the
     * model's answer, so it goes up.
     */
    if (inputs->mode == BUDDY_BRAIN_SMART) {
        if (inputs->phrase_matched &&
            inputs->phrase_confidence >= BUDDY_COMMAND_NEARLY_CONFIDENCE) {
            return VOICE_ROUTE_ANSWER_OFFLINE;
        }

        /*
         * And it asks for a real question's worth of speech, not the bare
         * minimum that proves the room was not silent. A stray word after the
         * wake word is much more likely to be a misfire than a question.
         */
        if (inputs->speech_seconds < BUDDY_SMART_MIN_SPEECH_SECONDS) {
            return VOICE_ROUTE_ANSWER_OFFLINE;
        }
    }

    if (inputs->requests_allowed > 0 && inputs->requests_used >= inputs->requests_allowed) {
        if (reason != NULL) {
            *reason = "I have used up my questions for this hour";
        }
        return VOICE_ROUTE_DECLINE_BUDGET;
    }

    return VOICE_ROUTE_ASK_CLOUD;
}

/* ------------------------------------------------------------------------ */
/* The hourly allowance                                                      */
/* ------------------------------------------------------------------------ */

void voice_budget_reset(voice_budget_t *budget)
{
    if (budget == NULL) {
        return;
    }
    budget->used = 0;
    budget->window_started_us = 0;
}

/**
 * @brief Start a new hour if the old one has run out.
 *
 * A jumping window rather than a sliding one, on purpose. A sliding window
 * needs a timestamp per request and answers a question nobody asks ("how many
 * in the last sixty minutes?"); a jumping one needs two numbers and answers the
 * question people do ask ("how many left?"). The cost is that an allowance can
 * be spent twice across a window boundary, which for a desk toy is not a
 * problem worth thirty lines of code.
 */
static void roll_window_if_needed(voice_budget_t *budget, int64_t now_us)
{
    /*
     * A first call, or a clock that went backwards — which happens on this
     * chip the first time SNTP lands and the clock jumps decades forward, and
     * would otherwise leave a window that never expires.
     */
    if (budget->window_started_us == 0 || now_us < budget->window_started_us) {
        budget->window_started_us = now_us;
        budget->used = 0;
        return;
    }

    if ((now_us - budget->window_started_us) >= BUDGET_WINDOW_US) {
        budget->window_started_us = now_us;
        budget->used = 0;
    }
}

uint16_t voice_budget_remaining(voice_budget_t *budget, int64_t now_us, uint16_t allowance)
{
    if (budget == NULL) {
        return 0;
    }
    roll_window_if_needed(budget, now_us);

    /* Zero means no ceiling at all, which reads better as "plenty" than as 0. */
    if (allowance == 0) {
        return UINT16_MAX;
    }
    if (budget->used >= allowance) {
        return 0;
    }
    return (uint16_t)(allowance - budget->used);
}

bool voice_budget_try_spend(voice_budget_t *budget, int64_t now_us, uint16_t allowance)
{
    if (budget == NULL) {
        return false;
    }
    roll_window_if_needed(budget, now_us);

    if (allowance > 0 && budget->used >= allowance) {
        return false;
    }

    if (budget->used < UINT16_MAX) {
        budget->used++;
    }
    return true;
}
