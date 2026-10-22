/*
 * voice_router.h — Deciding who answers: the chip, the cloud, or nobody.
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 *
 * WHAT THIS REPLACES
 * ------------------
 * The rule used to be one line, spread across two files: if the phrase
 * recogniser found nothing, send the recording to OpenAI. That is a policy, and
 * it was never written down anywhere as one — so every question about it ("why
 * did it pay to transcribe the television?", "how do I stop it spending?") had
 * to be answered by reading the control flow of voice_assistant.c.
 *
 * The policy now lives here, on its own, as a pure function of its inputs. No
 * hardware, no settings lookups, no network — just the decision, which means
 * the host tests can put it through every combination that matters instead of
 * hoping.
 *
 * THE FOUR MODES
 * --------------
 *   OFFLINE ONLY   Never reach for the cloud. The default, and a complete
 *                  product: a hundred-odd phrases, a face, a timer, sensors,
 *                  music, all free and all instant.
 *
 *   SMART          Reach for it only when the question is genuinely one the
 *                  chip cannot answer AND somebody genuinely asked it AND
 *                  there is budget left. This is the interesting mode and most
 *                  of what this file is about.
 *
 *   ALWAYS ASK     Anything the recogniser did not match goes to the cloud.
 *                  What the firmware used to do unconditionally, kept because
 *                  it is the right choice for someone who wants a talking
 *                  encyclopaedia and does not mind paying for one.
 *
 *   HOME AI        Anything the recogniser did not match goes to a small open
 *                  model on a computer on the same network (see
 *                  tools/home_brain/). The box's own AI: no account, no key,
 *                  no bill, so no budget. The speech gate and the "probably a
 *                  command" band still apply, because a home model still costs
 *                  several seconds of a person's time. When it is not set up,
 *                  or there is no Wi-Fi, the box answers offline instead of
 *                  apologising, the same as Smart does.
 *
 * WHY A BUDGET
 * ------------
 * A misfiring wake word in a noisy room can ask a hundred questions in an hour,
 * and the first anyone knows about it is the bill. An hourly allowance turns
 * the worst case from "unbounded" into a number the owner picked. Running out
 * is not an error — the buddy simply answers offline until the hour turns over,
 * which is exactly what it does in OFFLINE ONLY mode anyway.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "buddy_settings.h"

#ifdef __cplusplus
extern "C" {
#endif

/** What should happen with what was just heard. */
typedef enum {
    VOICE_ROUTE_ANSWER_OFFLINE = 0, /*!< The local brain has this one          */
    VOICE_ROUTE_ASK_CLOUD,          /*!< Worth spending a request on           */
    VOICE_ROUTE_NOTHING_HEARD,      /*!< No speech; say so and stop            */
    VOICE_ROUTE_DECLINE_NO_CLOUD,   /*!< Would ask, but it is off or unusable  */
    VOICE_ROUTE_DECLINE_BUDGET,     /*!< Would ask, but the allowance is spent */
    VOICE_ROUTE_ASK_HOME,           /*!< Worth a question to the home brain    */
} voice_route_t;

/** Everything the decision depends on, gathered in one place. */
typedef struct {
    /** Did the phrase recogniser match something, and how sure was it? */
    bool  phrase_matched;
    float phrase_confidence;

    /** What the recorder measured. See voice_speech_gate.h. */
    float recorded_seconds;
    float speech_seconds;

    /**
     * The loudest single sample in the recording, 0 to 32767.
     *
     * Deliberately separate from speech_seconds, and the two are used for
     * different decisions. speech_seconds comes from the room-relative gate and
     * answers "is this worth PAYING to transcribe" — a judgement call, tuned,
     * and capable of being wrong. The peak answers a much simpler question
     * that no tuning can get wrong: did the microphone produce anything at all?
     *
     * Only the peak may silence the buddy. See voice_router.c.
     */
    int32_t peak_amplitude;

    /** Is the cloud actually reachable and configured right now? */
    bool  cloud_ready;

    /**
     * Is a home brain address set and the Wi-Fi up? Only consulted in HOME AI
     * mode. It cannot say whether that computer is switched on; the request
     * finds that out quickly, see voice_cloud_brain.c.
     */
    bool  home_ready;

    /** How the owner wants this box to behave. */
    buddy_brain_mode_t mode;

    /** Requests already spent in the current hour, and the ceiling. */
    uint16_t requests_used;
    uint16_t requests_allowed;
} voice_route_inputs_t;

/**
 * @brief Decide who answers.
 *
 * @param inputs  The signals. Must not be NULL.
 * @param reason  Optionally receives a short, user-facing explanation of a
 *                decline — something the buddy can say out loud. NULL for the
 *                routes that need no explanation.
 */
voice_route_t voice_router_decide(const voice_route_inputs_t *inputs, const char **reason);

/* ------------------------------------------------------------------------ */
/* The hourly allowance                                                      */
/* ------------------------------------------------------------------------ */

/**
 * @brief A rolling count of cloud requests within the current hour.
 *
 * Zero-initialise it. The clock is passed in rather than read, so the host
 * tests can run a day through it in a millisecond.
 */
typedef struct {
    uint16_t used;
    int64_t  window_started_us;
} voice_budget_t;

/** @brief Forget everything spent so far. */
void voice_budget_reset(voice_budget_t *budget);

/**
 * @brief How many requests are left in the current hour.
 *
 * Rolls the window over first, so asking does not require having spent
 * anything recently.
 */
uint16_t voice_budget_remaining(voice_budget_t *budget, int64_t now_us, uint16_t allowance);

/**
 * @brief Spend one request, if there is one to spend.
 *
 * @return true when the request may go ahead.
 */
bool voice_budget_try_spend(voice_budget_t *budget, int64_t now_us, uint16_t allowance);

#ifdef __cplusplus
}
#endif
