/*
 * buddy_config.h — One place for every tunable number in the firmware.
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 *
 * Nothing in here talks to hardware. It is pure constants, so that when you
 * want the buddy to blink faster, listen longer, or move its eyes further, you
 * edit ONE file instead of hunting through the source.
 *
 * The sections below map 1:1 onto the source folders:
 *   main/hardware/  -> "Hardware" section
 *   main/voice/     -> "Voice assistant" section
 *   main/face/      -> "Face geometry" and "Face timing" sections
 *   main/ui/        -> "Screen layout" section
 *   main/audio/     -> "Audio" section
 */

#pragma once

/* =========================================================================
 * Product identity
 * ========================================================================= */

#define BUDDY_FIRMWARE_NAME             "ESP-BOX Desk Buddy"
#define BUDDY_FIRMWARE_VERSION          "3.7.1"

/* =========================================================================
 * Screen layout (the ESP32-S3-BOX-3 panel is 320 x 240 pixels)
 * ========================================================================= */

#define BUDDY_SCREEN_WIDTH              320
#define BUDDY_SCREEN_HEIGHT             240

/** Margin kept between the screen edge and the corner widgets. */
#define BUDDY_SCREEN_EDGE_MARGIN        6

/** Size of the round settings button parked in the bottom-left corner. */
#define BUDDY_SETTINGS_BUTTON_SIZE      38

/*
 * The caption bubble grows to fit its text rather than being a fixed slab.
 * "Okay!" gets a small bubble; a three-line answer gets a tall one. These are
 * the limits it grows between, not the size it always is.
 *
 * The width limit is the full screen minus the edge margins. That does NOT
 * make every bubble full width — the label inside is sized to its own text, so
 * a short caption still gets a short bubble. It only means a LONG caption is
 * allowed to use the whole width, which is what keeps the line count (and so
 * the height) down.
 */
#define BUDDY_SPEECH_BUBBLE_MAX_WIDTH   (BUDDY_SCREEN_WIDTH - (2 * BUDDY_SCREEN_EDGE_MARGIN))
#define BUDDY_SPEECH_BUBBLE_MIN_WIDTH   64

/*
 * How tall the bubble may grow: the whole screen, less its margins.
 *
 * This used to be 128 — set so the bubble always stopped below the lifted eyes,
 * and the buddy could look at you over the top of whatever it was saying. It
 * was a nice idea that cost more than it was worth: a long answer ran out of
 * room and was ended with an ellipsis, so the interesting half of the sentence
 * was the half you could not read.
 *
 * Reading the answer beats seeing the face while it is read. The bubble now
 * grows as far as it needs to and covers the face if the text demands it, then
 * gets out of the way again when the caption expires.
 *
 * At the smaller of the two caption fonts this holds around twelve lines —
 * comfortably more than BUDDY_REPLY_TEXT_MAX_LEN can hold at that width, so in
 * practice nothing is ever shortened.
 */
#define BUDDY_SPEECH_BUBBLE_MAX_HEIGHT  (BUDDY_SCREEN_HEIGHT - (2 * BUDDY_SCREEN_EDGE_MARGIN))

/** Padding between the bubble's border and the text inside it. */
#define BUDDY_SPEECH_BUBBLE_PADDING     9

/*
 * How far the face slides up to clear the bubble.
 *
 * Scaled to the bubble that is actually on screen — a one-word caption should
 * barely move the face, where a long answer needs real room. The face never
 * moves more than the cap below, however tall the bubble gets.
 */
#define BUDDY_FACE_LIFT_DIVISOR         3
#define BUDDY_FACE_LIFT_MAX             34

/** How long a speech bubble stays on screen after the buddy stops talking. */
#define BUDDY_SPEECH_BUBBLE_LINGER_MS   4000

/* =========================================================================
 * Face geometry — all coordinates are relative to the middle of the screen
 * ========================================================================= */

/*
 * OVERALL FACE SIZE
 * -----------------
 * Every dimension of the face is expressed as a percentage of the original
 * full-bleed design, so you can make the buddy bigger or smaller by changing
 * this ONE number and nothing else.
 *
 *   100 = the original size, which crowds the sensor pill in the top-right
 *    86 = the current setting: clearly smaller, nothing collides
 *    75 = a small face with a lot of white space around it
 *
 * Positions (the *_CENTRE_Y values below) are deliberately NOT scaled — they
 * say where the face sits on the screen, which is a separate decision from how
 * big it is.
 */
#define FACE_SCALE_PERCENT              86
#define FACE_SCALED(pixels)             (((pixels) * FACE_SCALE_PERCENT) / 100)

/** Horizontal distance from screen centre to the centre of each eye. */
#define FACE_EYE_SPACING_FROM_CENTRE    FACE_SCALED(64)

/** Vertical position of the eye centres, measured from the top of the screen. */
#define FACE_EYE_CENTRE_Y               114

/** Resting eye size. Eyes are rounded rectangles, "kawaii" style. */
#define FACE_EYE_WIDTH                  FACE_SCALED(60)
#define FACE_EYE_HEIGHT                 FACE_SCALED(74)
#define FACE_EYE_CORNER_RADIUS          FACE_SCALED(26)

/** Height an eye shrinks to at the closed point of a blink. */
#define FACE_EYE_HEIGHT_WHEN_CLOSED     6

/** Maximum pixels the eyes travel when looking around. */
#define FACE_EYE_LOOK_RANGE_X           FACE_SCALED(16)
#define FACE_EYE_LOOK_RANGE_Y           FACE_SCALED(12)

/** Eyebrow bar: length, thickness, and resting distance above the eye. */
#define FACE_BROW_LENGTH                FACE_SCALED(62)
#define FACE_BROW_THICKNESS             FACE_SCALED(9)
#define FACE_BROW_GAP_ABOVE_EYE         FACE_SCALED(30)

/*
 * A hard floor on how high an eyebrow may be drawn.
 *
 * Surprise lifts the brows, looking up lifts them further, and an antic that
 * bounces the whole face upward lifts them further still. Stacked together
 * those can push a brow into the temperature pill in the top-right corner.
 * Rather than trying to keep every combination in range by hand, the brow
 * position is simply clamped here — the extreme expressions lose a pixel or
 * two of lift, and nothing can ever collide again.
 */
#define FACE_BROW_MIN_BASELINE_Y        40

/** Mouth: vertical position (from top of screen) and resting width. */
#define FACE_MOUTH_CENTRE_Y             186
#define FACE_MOUTH_WIDTH                FACE_SCALED(96)
#define FACE_MOUTH_THICKNESS            FACE_SCALED(10)

/** Number of points used to draw the curved mouth line. Must be odd and >= 3. */
#define FACE_MOUTH_POINT_COUNT          9

/** Widest the mouth opens while speaking, in pixels. */
#define FACE_MOUTH_MAX_OPEN_HEIGHT      FACE_SCALED(44)

/** Blush circles that appear on happy/embarrassed expressions. */
#define FACE_BLUSH_DIAMETER             FACE_SCALED(30)
#define FACE_BLUSH_OFFSET_X             FACE_SCALED(92)
#define FACE_BLUSH_OFFSET_Y             154

/**
 * Lowest pixel the temperature/humidity pill can reach.
 *
 * The pill is laid out by LVGL from its font and padding, so this is a stated
 * upper bound rather than a measurement — it is what the face is kept clear of,
 * and the host tests check the two against each other.
 */
#define FACE_SENSOR_CHIP_BOTTOM_Y       32

/* =========================================================================
 * Face timing
 * ========================================================================= */

/** Default cross-fade duration when moving between two expressions. */
#define FACE_EXPRESSION_TRANSITION_MS   260

/** One eyelid close+open cycle. */
#define FACE_BLINK_CLOSE_MS             70
#define FACE_BLINK_OPEN_MS              110

/** Random gap between spontaneous blinks. */
#define FACE_BLINK_INTERVAL_MIN_MS      2600
#define FACE_BLINK_INTERVAL_MAX_MS      6400

/** Random gap between spontaneous idle antics (look around, wink, stretch...). */
#define FACE_IDLE_ANTIC_INTERVAL_MIN_MS 7000
#define FACE_IDLE_ANTIC_INTERVAL_MAX_MS 17000

/** How often the mouth shape is refreshed while the buddy is speaking. */
#define FACE_LIP_SYNC_REFRESH_MS        55

/** Minutes of no motion / no interaction before the face dozes off. */
#define FACE_SLEEPY_AFTER_MINUTES       5

/* =========================================================================
 * Hardware
 * ========================================================================= */

/** Microphone/codec sample format used by the whole audio chain. */
#define BUDDY_AUDIO_SAMPLE_RATE_HZ      16000
#define BUDDY_AUDIO_BITS_PER_SAMPLE     16
#define BUDDY_AUDIO_CHANNEL_COUNT       2

/** How often the temperature/humidity readout on screen is refreshed. */
#define BUDDY_SENSOR_REFRESH_MS         3000

/** Speaker volume applied to short notification chimes (0-100). */
#define BUDDY_CHIME_VOLUME              85

/* =========================================================================
 * Voice assistant
 * ========================================================================= */

/*
 * After the wake word fires, MultiNet listens for this many milliseconds.
 * If one of the built-in phrases matches inside that window we answer locally
 * and instantly. If the window expires without a match, whatever was recorded
 * is treated as a free-form question and (optionally) sent to the cloud.
 */
#define BUDDY_LISTEN_WINDOW_MS          6000

/*
 * The wake word, spelled the way a person would read it aloud rather than the
 * way the model is named. Shown on the Question Examples screen so the user is
 * never guessing what to say first.
 */
#define BUDDY_WAKE_WORD_ENGLISH         "Hi E.S.P."
#define BUDDY_WAKE_WORD_CHINESE         "Hi Lexin"

/** How long the first-boot greeting stays on screen, in milliseconds. */
#define FIRST_RUN_HINT_MS               12000

/** How long a face previewed from Settings is held before it relaxes. */
#define EXPRESSION_PREVIEW_MS           4000

/*
 * How sure the phrase recogniser has to be before a match is acted on.
 *
 * There are around ninety phrases registered with MultiNet. It always returns
 * the best of them, and "best" is not the same as "right": ask a real question
 * and it will find whichever registered phrase sounds least unlike it, so
 * "what's the capital of France" can come back as "what is the date". The
 * canned answer is then given, and the question never reaches the online brain
 * at all — which looks exactly like the online brain being broken.
 *
 * A match below this confidence is treated as "no phrase matched", which is the
 * path that sends the recording to the cloud.
 *
 * WHY THIS IS 0.10 AND NOT 0.35 — THE v3.7 CORRECTION
 * ---------------------------------------------------
 * It was 0.35, and that number was wrong. It was picked by assuming prob[0] is
 * a calibrated confidence, the way a classifier's softmax output usually is.
 * MultiNet 6's is not. It routinely reports 0.14 to 0.30 on matches that are
 * perfectly correct, so a floor of 0.35 did not reject the occasional bad
 * guess — it rejected almost EVERY command, including ones said clearly into a
 * quiet room. Every built-in phrase fell through to the "treat it as a
 * question" path, and with no online brain to catch it the box answered
 * "I did not hear anything" to everything it was asked. Two thresholds tuned
 * independently, stacking into total deafness.
 *
 * The real protection against a wrong match is not the absolute score, it is
 * the SEPARATION between the best phrase and the runner-up — see
 * BUDDY_COMMAND_MARGIN_OVER_RUNNER_UP below. A genuine command wins clearly; a
 * question being force-fitted to the nearest phrase produces several near-ties.
 * That comparison is scale-free, so it does not care how MultiNet chooses to
 * normalise its numbers.
 *
 * So this is now what it always claimed to be: a floor low enough that it
 * cannot break a phrase that used to work. MultiNet applies a threshold of its
 * own before reporting a match at all, and reaching ESP_MN_STATE_DETECTED is
 * already its decision that a phrase matched. Second-guessing that with a
 * number of our own is what caused this bug.
 *
 * Every match prints its score whether accepted or not, so tuning is a matter
 * of reading the monitor rather than guessing:
 *
 *     I (12345) wakeword: Recognised "what is the date" (0.62, runner-up 0.11)
 *
 *   0.00   accept whatever MultiNet reports
 *   0.10   current setting: rejects only noise-level scores
 *   0.35   what broke it; do not go back here
 */
#define BUDDY_COMMAND_MIN_CONFIDENCE    0.10f

/*
 * How far clear of the SECOND-best phrase the winner has to be.
 *
 * This is the test that actually separates "they said a command" from "they
 * asked a question and MultiNet found the least unlike phrase". A real command
 * beats the runner-up comfortably. A question force-fitted to the vocabulary
 * produces a cluster of similar scores, because nothing really matched.
 *
 * Expressed as a ratio of the winner's own score, so it works whatever scale
 * MultiNet reports on: the runner-up must be below 70% of the winner.
 */
#define BUDDY_COMMAND_MARGIN_OVER_RUNNER_UP 0.70f

/*
 * The "that was probably a command, said badly" band, used in Smart mode only.
 *
 * Between this and BUDDY_COMMAND_MIN_CONFIDENCE the recogniser is unsure but
 * not lost: it heard something shaped like a phrase it knows. Sending that to a
 * language model buys a courteous paragraph about a command the box could
 * simply have obeyed, so Smart mode keeps it. Always-ask mode does not, because
 * somebody who chose that mode wants the model's answer.
 *
 * Lowered alongside the floor above, and for the same reason: it was 0.22,
 * which on MultiNet's actual scale is most of the useful range rather than the
 * bottom of it.
 */
#define BUDDY_COMMAND_NEARLY_CONFIDENCE 0.05f

/*
 * How much speech Smart mode wants before it will spend a request.
 *
 * Higher than BUDDY_MIN_SPEECH_SECONDS, which only asks "was the room silent?".
 * A real question is a sentence; a single word after the wake word is far more
 * often a misfire, and answering it offline costs nothing while getting it
 * wrong costs money.
 */
#define BUDDY_SMART_MIN_SPEECH_SECONDS  0.55f

/*
 * Cloud requests allowed per hour, as the factory default.
 *
 * A wake word misfiring in a noisy room can ask a great many questions before
 * anybody notices, and the first sign is usually the bill. Thirty an hour is
 * far more than a person asks and far less than a fault produces. Zero means no
 * ceiling; the owner can change it in Settings.
 */
#define BUDDY_DEFAULT_REQUESTS_PER_HOUR 30

/*
 * How many phrases may be registered with the recogniser in one language.
 *
 * Raised from 100 in v3.3, when the conversational phrases took English past
 * that. This was never a MultiNet limit — the model copes with a couple of
 * hundred — it is the size of the lookup arrays in voice_wakeword.c, and
 * exceeding it silently dropped whatever came last.
 *
 * There is a real cost to a long list, though, and it is not memory: the
 * recogniser always returns its closest match, so every extra phrase is one
 * more thing an unrelated sentence can be mistaken for. Add phrases that earn
 * their place, and keep them distinctive — three to five syllables, no rhymes.
 */
#define BUDDY_MAX_REGISTERED_PHRASES    160

/* -------------------------------------------------------------------------
 * Deciding whether a recording is worth paying to transcribe
 *
 * The online brain charges per request, so the buddy should only reach for it
 * when somebody genuinely asked something. Before v3.4 anything that followed
 * the wake word went off to be transcribed — a cough, a door, the television,
 * or the very common case of saying the wake word and then thinking better of
 * it. See voice_speech_gate.h for how the measurement works.
 *
 * Both thresholds are printed in the log on every listen, so tuning them is a
 * matter of reading what your room actually produces rather than guessing:
 *
 *     I (12345) recorder: Captured 2.31 s, 0.94 s of it speech (peak 8213)
 * ------------------------------------------------------------------------- */

/**
 * How loud a 10 ms block has to be to count as speech, on a scale where a
 * sample can reach 32767.
 *
 * The microphone runs through the front end's automatic gain control, so
 * conversation at desk distance lands in the thousands and a quiet room sits in
 * the low hundreds. 900 is comfortably between the two. Raise it if the buddy
 * reacts to background noise; lower it if it ignores you when you speak softly.
 */
#define BUDDY_SPEECH_LOUDNESS_THRESHOLD 900

/*
 * How far above the ROOM's own background speech has to sit, as a multiple of
 * amplitude.
 *
 * The absolute figure above is a floor for a silent room. This is what makes
 * the box work in a room that is not silent: it measures what it sounds like
 * when nobody is talking, and asks speech to stand out from that. Six times
 * the amplitude is roughly sixteen decibels over the background — above a fan
 * or a television two rooms away, below anybody addressing the box directly.
 *
 * Settings scales this: Quiet room lowers it so a whisper still registers,
 * Noisy room raises it so the background does not.
 */
#define BUDDY_SPEECH_OVER_ROOM_MARGIN   6

/*
 * The highest the room may ever push the speech threshold.
 *
 * Without this, the threshold is max(absolute floor, room x margin) with no
 * upper bound — and the room estimate is measured, not chosen. A box left next
 * to a running extractor fan, or one that learns a loud room and then has the
 * Noisy-room profile applied on top (margin 10, so a hundredfold in energy),
 * can end up asking for a block louder than a human voice can produce. The
 * result is a box that is permanently deaf with no indication why, which is a
 * far worse failure than occasionally transcribing a fan.
 *
 * 6000 is loud — roughly a raised voice a hand's width from the microphone.
 * Speech at desk distance lands in the low thousands, so a threshold at this
 * ceiling is already saying "this room is too loud for the gate to help".
 * Better to let a few false positives through than to go silent.
 */
#define BUDDY_SPEECH_THRESHOLD_CEILING  6000

/*
 * The peak sample level that counts as "the microphone produced something".
 *
 * This is the ONLY loudness test that is allowed to make the buddy say "I did
 * not hear anything", and it is deliberately far below anything tuned. It is
 * not asking whether the sound was speech, or whether it stood out from the
 * room — only whether the recording is distinguishable from a dead microphone.
 *
 * Erring low is the whole point. A false pass costs one offline answer nobody
 * needed; a false fail is a box that ignores its owner. On a 0 to 32767 scale,
 * conversation peaks in the thousands and a quiet room in the low hundreds, so
 * 500 sits below any real speech while still rejecting silence.
 */
#define BUDDY_AUDIBLE_PEAK_AMPLITUDE    500

/** Shortest recording worth sending. Below this, nobody finished a sentence. */
#define BUDDY_MIN_QUESTION_SECONDS      0.7f

/** How much of it has to be loud. A question is mostly talking. */
#define BUDDY_MIN_SPEECH_SECONDS        0.35f

/** Upper bound on a recorded question, in seconds, to cap PSRAM use. */
#define BUDDY_MAX_RECORDING_SECONDS     10

/*
 * How long the recording buffer may stay claimed before the claim is ignored.
 *
 * There is one recording buffer, and a cloud request holds it while the audio
 * is uploaded so a second wake word cannot overwrite bytes mid-transfer. Every
 * path that finishes a request releases it. The trouble is what a MISSED
 * release looks like: voice_recorder_start() quietly declines to record, every
 * later question measures zero seconds long, and the box says "I did not hear
 * anything" forever while still visibly waking up. Indistinguishable, from the
 * outside, from a broken microphone.
 *
 * So the claim expires. Comfortably longer than
 * BUDDY_CLOUD_REQUEST_DEADLINE_S, so a live request always wins the race;
 * short enough that a leaked claim costs one question rather than the rest of
 * the session. Written as a literal because it sits above that definition, and
 * a reader should not have to scroll to know what the number is.
 */
#define BUDDY_RECORDER_LOCK_MAX_HOLD_S  35

/** Bytes reserved for one recorded question (mono 16-bit at the sample rate). */
#define BUDDY_RECORDING_BUFFER_BYTES \
    (BUDDY_MAX_RECORDING_SECONDS * BUDDY_AUDIO_SAMPLE_RATE_HZ * sizeof(int16_t))

/** Longest reply text we keep around for the speech bubble. */
#define BUDDY_REPLY_TEXT_MAX_LEN        512

/** Longest transcribed question we keep around. */
#define BUDDY_QUESTION_TEXT_MAX_LEN     256

/* =========================================================================
 * Audio / music
 * ========================================================================= */

/** Folder inside the SPIFFS "storage" partition scanned for .mp3 files. */
#define BUDDY_MUSIC_FOLDER              "/spiffs/music"

/** Folder holding the short notification chimes. */
#define BUDDY_PROMPT_FOLDER             "/spiffs"

/** Largest streamed track we will pull into PSRAM before playing, in bytes. */
#define BUDDY_STREAM_MAX_TRACK_BYTES    (3 * 1024 * 1024)

/** Maximum number of local tracks remembered by the playlist. */
#define BUDDY_MUSIC_MAX_TRACKS          32

/* =========================================================================
 * Networking
 * ========================================================================= */

/** Attempts before giving up on a Wi-Fi access point. */
#define BUDDY_WIFI_MAX_RETRIES          5

/** Seconds to wait for an IP address before declaring the join failed. */
#define BUDDY_WIFI_CONNECT_TIMEOUT_S    15

/** Default OpenAI-compatible endpoint. Override it in Settings if you proxy. */
#define BUDDY_DEFAULT_CLOUD_BASE_URL    "https://api.openai.com/v1/"

/*
 * Where the API key may be flashed from, instead of typed on the touchscreen.
 *
 * Put the key on the first line of spiffs/openai_key.txt in the project folder
 * and it lands here as part of the storage partition. See buddy_settings.h for
 * which key wins when both a file and a saved key exist.
 */
#define BUDDY_API_KEY_FILE              "/spiffs/openai_key.txt"

/*
 * Seconds to wait for the clock before giving up on a cloud request.
 *
 * HTTPS verifies that a certificate is valid TODAY, and the chip boots
 * believing it is 1 January 1970 — a date before every certificate on the
 * internet was issued. Until the network time client has answered, every
 * handshake with api.openai.com fails, and it fails with a TLS error that says
 * nothing about clocks.
 *
 * In practice the clock is set a second or two after the box gets an IP
 * address, so this only matters for a question asked immediately after boot.
 * Waiting a few seconds turns a baffling failure into no failure at all.
 */
#define BUDDY_CLOCK_WAIT_FOR_TLS_S      8

/*
 * How long a cloud request may keep the microphone shut.
 *
 * THE DEAD END THIS EXISTS TO PREVENT
 * -----------------------------------
 * A request holds listening closed so that a second wake word cannot overwrite
 * the recording being uploaded. That is correct while the request is moving.
 * It is a trap when the request is NOT moving — a TLS handshake that never
 * completes, a socket that never answers, a network that vanished mid-call.
 * The OpenAI component has no timeout of its own to set, so from the outside
 * such a call simply never returns.
 *
 * The watchdog could not save the buddy from that, because the watchdog asks
 * "is a request in flight?" and the honest answer was yes — for ever. The box
 * sat there looking perfectly alive and deaf to the wake word until it was
 * power cycled.
 *
 * So the ears come back on a clock instead. By ten seconds the recording has
 * long since been uploaded and what is left is waiting for an answer, which is
 * not a reason to stay deaf. The request carries on in the background; if it
 * eventually succeeds the answer still arrives.
 */
#define BUDDY_LISTENING_RETURNS_AFTER_S 10

/*
 * And how long before the request itself is written off.
 *
 * Longer than the figure above, because this one has a real cost: a working
 * request that takes its time is transcribe, then answer, then speak, and that
 * genuinely runs to ten or fifteen seconds over a slow connection. Twenty five
 * is past anything that is still going to work.
 *
 * After this the reply is ignored even if it turns up, so a response that
 * arrives two minutes late cannot suddenly start talking over whatever the
 * buddy is doing by then.
 */
#define BUDDY_CLOUD_REQUEST_DEADLINE_S  25

/*
 * Models used for the cloud steps. Transcription is fixed at whisper-1 by the
 * OpenAI component itself, so there is no knob for it here.
 */
#define BUDDY_CLOUD_CHAT_MODEL          "gpt-4o-mini"
#define BUDDY_CLOUD_SPEECH_MODEL        "tts-1"
#define BUDDY_CLOUD_SPEECH_VOICE        "nova"

/** Cap on reply length so the buddy stays snappy and the bubble stays readable. */
#define BUDDY_CLOUD_MAX_REPLY_TOKENS    160

/*
 * The home brain: tools/home_brain/home_brain.py on a computer on the LAN.
 *
 * The model names are labels, not choices. The home brain answers with
 * whatever model it was started with (see its --model flag), so the box does
 * not need reflashing to try a different one.
 */
#define BUDDY_HOME_CHAT_MODEL           "home-brain"
#define BUDDY_HOME_SPEECH_MODEL         "home-voice"

/*
 * The home brain needs no key, but the OpenAI component always sends an
 * Authorization header. This placeholder is what it sends; it is not a secret
 * and the home brain ignores it.
 */
#define BUDDY_HOME_PLACEHOLDER_KEY      "no-key-needed"

/*
 * How long to wait for the home brain to say it is awake, before a question.
 *
 * The OpenAI component's own timeout is a minute, and a computer that is
 * switched off does not refuse a connection — it simply never answers. Without
 * this check a question to a sleeping PC would sit on "Thinking..." until the
 * request deadline. With it, the box knows within about a second and says so.
 */
#define BUDDY_HOME_BRAIN_PROBE_TIMEOUT_MS 1500
