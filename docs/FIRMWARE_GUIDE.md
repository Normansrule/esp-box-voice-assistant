> This is the original, detailed Desk Buddy guide (Windows-first). The short version is in the [README](../README.md).

# ESP-BOX Desk Buddy

A cute face that lives on your desk, listens for a wake word, answers questions,
plays music, and pulls a face when you poke it.

Built for the **ESP32-S3-BOX-3** with the sensor dock attached.

---

## What it does

**A full-screen face.** Espressif red on white, drawn entirely from LVGL shapes —
no bitmaps, no artwork files. Two rounded eyes, two eyebrows and a mouth, all of
which move. Thirty-six expressions and twelve idle antics.

**It blinks and fidgets on its own.** Every few seconds it blinks; every ten to
twenty seconds it does something small — glances around, winks, yawns, shakes its
head, rolls its eyes. Tap the screen and it reacts immediately with a random one.
Ignore it for five minutes and it gets sleepy.

**Its mouth moves when it talks.** The speaker driver measures how loud the audio
leaving the box is, and the mouth opening follows that number. It genuinely
mouths along with its own voice rather than flapping on a timer.

**It listens for "Hi E.S.P."** and then either answers instantly from an offline
phrase list, or — for anything else — sends what it heard to an online service
and speaks the answer back.

**It shows the temperature and humidity** in the top right, read from the sensor
dock. Tap the gear in the bottom left for everything else.

**It keeps a timer for you.** "Set a timer for ten minutes" starts a countdown
in the top-left corner, which turns red in the last thirty seconds and then
says so out loud. No network involved.

**It notices when you sit down.** The sensor dock has a microwave radar on it.
When the desk has been empty for a while and somebody arrives, the buddy looks
up — quietly, with no chime and no greeting, because a box that announces
itself every time you walk past stops being charming within a day.

---

## Part one: installing ESP-IDF

ESP-IDF is Espressif's build system and toolchain. `idf.py` is part of it, which
is why that command is not recognised until this is done.

**Install ESP-IDF v5.5.x — not v6.0 or newer.** ESP-IDF v6 removed the legacy
I2C driver that this board's sensor-dock support still uses, so the build will
fail on v6 with errors about `i2c_param_config` and friends.

### 1. Download the installer

Go to <https://dl.espressif.com/dl/esp-idf/> and download:

> **ESP-IDF v5.5.5 — Offline Installer** (about 1.6 GB)

The offline installer bundles Git, Python, the compiler and every tool in one
package. The online installer downloads them during setup, which takes longer
and fails awkwardly if the network hiccups. Take the offline one.

### 2. Run it

Accept the defaults. Two things worth knowing as you click through:

- **Install path.** The default is `C:\Espressif`. Keep it. Long paths and
  spaces have historically caused trouble in this toolchain, and your project
  lives on the Desktop anyway.
- **Components.** Leave everything ticked. The ESP32-S3 support, the USB
  drivers and the Windows Terminal profile are all wanted.

It takes roughly ten to fifteen minutes and asks for administrator rights once,
to install the USB serial drivers.

### 3. Open an ESP-IDF terminal

This is the step people miss, and it is exactly why `idf.py` was not recognised.

`idf.py` is **not** on your normal PATH. It only exists inside a shell that has
had the ESP-IDF environment loaded into it. The installer creates a Start-menu
shortcut for that:

> Start menu → **ESP-IDF 5.5 PowerShell**

Open that, not a normal PowerShell window. You will see it print something like
`Setting IDF_PATH: C:\Espressif\frameworks\esp-idf-v5.5.5` on startup.

Check it worked:

```powershell
idf.py --version
```

You should get `ESP-IDF v5.5.5`. If you do, you are ready.

<details>
<summary>Using a normal PowerShell window instead</summary>

If you would rather not use the shortcut, you can load the environment into any
PowerShell session by hand:

```powershell
C:\Espressif\frameworks\esp-idf-v5.5.5\export.ps1
```

This has to be run again in every new window — the environment does not persist.
If PowerShell refuses to run it, allow scripts for that one session first:

```powershell
Set-ExecutionPolicy -Scope Process -ExecutionPolicy Bypass
```

</details>

---

## Part two: building the firmware

> **The project folder must not have a space anywhere in its path.** The
> speech recognition component hands its library folders to the linker without
> quoting them, so a path like `Desktop\ESP-Box Voice Assistant\` gets chopped
> in half at the space and the link fails with `cannot find Voice`. Keep the
> folder named `ESP-Box-Voice-Assistant`, or move it somewhere like `C:\esp\`.
> This is a limitation of `esp-sr`, not of this project — but it bites late, at
> the very last step of a ten-minute build, so it is worth getting right first.

In the **ESP-IDF 5.5 PowerShell** window — the shortcut opens in the ESP-IDF
install folder, so you have to change directory to the project every time:

```powershell
cd $env:USERPROFILE\Desktop\esp-box-voice-assistant
idf.py set-target esp32s3
idf.py build
```

The first build takes a while — five to ten minutes — because it downloads the
managed components and compiles the whole of ESP-IDF. Later builds take
seconds.

Then plug the box in over USB-C and:

```powershell
idf.py flash monitor
```

The first flash writes the speech models, which is slow and only happens once.
Press `Ctrl` + `]` to leave the monitor.

<details>
<summary>If it cannot find the board</summary>

Name the port explicitly. To see which ports exist:

```powershell
[System.IO.Ports.SerialPort]::GetPortNames()
```

Then use it:

```powershell
$env:ESP_PORT = "COM9"
idf.py -p $env:ESP_PORT flash monitor
```

The BOX-3 shows up as a USB serial device as soon as it is plugged in. If it
does not appear at all, hold the **BOOT** button on the back while connecting
the cable to force it into download mode.

</details>

### Then set it up on the box itself

1. Tap the gear in the bottom-left corner.
2. Under **START HERE**, tap Wi-Fi, pick your network, type the password.

That is the whole setup. Wi-Fi is only there for the clock — the face, the
sensors, the timer, the music and every phrase in the offline vocabulary work
with no network at all, and cost nothing to run.

### Choosing when it uses AI

Four modes, in **gear → ONLINE BRAIN → When to ask**:

| Mode | What it does | What it costs |
| --- | --- | --- |
| **Never** | Everything on the chip. The default. | Nothing |
| **Only when stuck** | Answers on the chip wherever it can; pays only for what it genuinely cannot do | Rarely |
| **Always** | Anything the recogniser did not match goes up | Per question |
| **Home AI** | Anything unmatched goes to a small open AI on your own computer (Whisper + llama3.2 via Ollama), never to OpenAI. See [tools/home_brain](../tools/home_brain/README.md) | Nothing |

Home AI needs no key and has no hourly limit. If that computer is off, the box
finds out in about a second and answers offline instead. Untested on hardware
so far.

"Only when stuck" is the interesting one. Before spending a request it checks
four things: the phrase recogniser did not confidently match (a recognised
command is *never* sent up, in any mode — paying to be told the time when the
box has a clock is slower as well as wasteful); the recogniser did not
*nearly* match either, which usually means a command said badly; there was a
sentence's worth of actual speech, not a cough or a stray word; and the hourly
allowance has room.

That allowance is the other half. A wake word misfiring in a noisy room can ask
a hundred questions before anyone notices, and the first sign is the bill —
so there is a ceiling, defaulting to 30 an hour, with the count left showing in
Settings. Running out is not an error: the buddy answers offline until the hour
turns over, which is what it does in "Never" mode anyway.

The whole policy is one pure function in `main/voice/voice_router.c`, which is
why the host tests can put it through every combination rather than hoping.

### The online brain is optional, and off

There is a second mode where questions the recogniser does not know get sent to
OpenAI, which answers in a natural voice. It is genuinely nice and it is
genuinely not free: every question bills your OpenAI account.

So it ships **switched off**, it stays off until you turn it on yourself in
Settings → ONLINE BRAIN, and nothing else in the firmware nags you about it.
Flashing an API key does not switch it on either — the key is stored, ready, and
inert until you flip the switch.

Everything below this heading is for people who want that mode. Skip it
otherwise; you are not missing a working buddy.

### The API key, without typing 165 characters on a touchscreen

Get a key from [platform.openai.com/api-keys](https://platform.openai.com/api-keys).
Note the length: OpenAI's original keys were 51 characters, but the project keys
the console issues today (`sk-proj-…`) run to about **165**. Typing that on a
320x240 screen is unpleasant, and one wrong character is indistinguishable from
a wrong key.

So you can flash it instead. Copy `spiffs/openai_key.example.txt` to
`spiffs/openai_key.txt` (that name is git-ignored), replace the first
line with your key, and rebuild:

```
idf.py build
idf.py -p <your COM port> flash
```

Only the first line is read; everything below it is notes, and they can stay.
The flashed file wins whenever it differs from what is saved, so rotating a key
is "edit the file, reflash" — no hunting through settings. To manage the key on
the touchscreen instead, just leave the placeholder line alone.

Then turn it on: **gear → ONLINE BRAIN (OPTIONAL) → Answer any question**. The
key alone does nothing until that switch is on, which is deliberate — a firmware
update should not be able to start spending your money because a file happened
to be in the build.

**Check it took.** The boot log prints the length, never the key:

```
I (4210) selftest:   [ok  ] Online brain   sk-proj-... 164 chars
```

Settings shows the same number beside **API key**. If it reads noticeably short,
the key is incomplete — that is the failure this display exists to make visible,
because OpenAI answers a truncated key and a wrong key with the same 401.

Two things worth knowing: the key is stored unencrypted, in flash and in NVS, so
anyone who can plug the box into a USB port can read it back — use a
project-scoped key with a low monthly limit. And if you ever put this project in
git: `spiffs/openai_key.txt` is already in `.gitignore` here. Keep it that way.

---

## Talking to it

Say **"Hi E.S.P."**, wait for the chirp, then speak.

A good first test, once it is on your desk: say **"Hi E.S.P."**, wait for the
chirp, then **"show me a smile"**. No Wi-Fi and no API key needed — if the face
grins back at you, the microphone, the recogniser, the screen and the speaker
are all working.

The full list lives on the device: **gear -> Question examples**. It is built
from the same table the recogniser is loaded from, so it can never drift out of
step with what actually works.

### Phrases it knows offline

These are recognised on the chip itself. No network, no delay, no cost.

| Say | It does |
| --- | --- |
| "hello buddy" / "hey there little guy" | greets you |
| "how are you doing" | tells you how it is |
| "what can you do" | lists a few example phrases |
| "tell me a joke" / "make me laugh" | tells a joke |
| "who made you" | explains itself |
| "goodbye buddy" / "go to sleep now" | dozes off |
| "what time is it" / "tell me the time" | tells the time |
| "what is the date" / "what day is it" | tells the date |
| "what is the temperature" / "how warm is it" | reads the sensor |
| "what is the humidity" / "how humid is it" | reads the sensor |
| "play some music" / "sing me a song" | plays a random track |
| "play the next song" / "skip this track" | next track |
| "stop the music" / "be quiet please" | stops |
| "show me a smile" / "be happy now" | smiles |
| "look very sad" | looks sad |
| "act surprised now" | jumps |
| "give me a wink" | winks |
| "do something silly" / "make a funny face" | random antic |
| "look pleased with yourself" | smug |
| "act totally shocked" | shocked |
| "look determined now" | determined |
| "have a little giggle" | giggly |
| "look suspicious of me" | suspicious |
| "act relieved now" | relieved |
| "look totally amazed" | amazed |
| "act a bit nervous" | nervous |
| "give me puppy eyes" | pleading |
| "celebrate with me" | triumphant |
| "look unimpressed" | unimpressed |
| "scrunch your face up" | scrunch |
| "act all shy now" | shy |
| "start panicking now" | panic |
| "look totally disgusted" | disgusted |
| "sing along with me" | singing |
| "set a timer for ten minutes" (and 1, 2, 5, 20, 30) | starts a countdown |
| "how long is left" / "check my timer" | reads the countdown |
| "cancel the timer" / "stop the timer" | stops it |
| "flip a coin" / "heads or tails" | decides for you |
| "roll a dice" / "roll the dice for me" | rolls one |
| "rock paper scissors" / "let us play a game" | throws one |
| "decide for me" / "should i do it" | gives a yes or a no |
| "what is your name" / "who are you" | answers |
| "how old are you" | answers |
| "are you a robot" | answers |
| "thank you buddy" | says you are welcome |
| "you are the best" / "i really like you" | gets embarrassed |
| "good morning buddy" | says good morning |
| "give me a high five" | high fives, badly |
| "tell me a fact" / "teach me something" | tells you something true |
| "count to ten" | counts to ten |
| "what is two plus two" and seven more sums | works it out on the chip |
| "turn the volume up" / "down" | adjusts volume |
| "open the settings" | opens settings |

| "how long have you been on" | uptime, spoken |
| "how is your memory" | free internal RAM, spoken |
| "are you online" | network status and address |
| "what is your wake word" | reminds you |
| "give me some advice" | something practical |
| "read my fortune" | something cheerful |
| "tell me a story" | a very short one |
| "cheer me up" | tries its best |

Plus **48 expressions** you can ask for by name — "look amazed at me", "give me
a doubtful look", "daydream a little", "get comfortable", "concentrate very
hard" and so on. The full list is on the box: **gear → Question examples**.

Chinese equivalents are in the table too — switch language in Settings and the
wake word becomes "Hi Lexin".

### The calculator

Say **"open the calculator"** (or **"let me do a sum"**), or tap it in Settings.

A keypad, and it **says the answer out loud** — offline, free, instantly. This
is the answer to "let me ask it any calculation": the phrase recogniser can only
match phrases registered in advance, so no arrangement of a vocabulary covers
arbitrary arithmetic. A keypad does.

Four functions plus percent, sign and backspace, evaluated left to right the way
a pocket calculator works rather than the way algebra does — `2 + 3 × 4` gives
20, not 14, and that is deliberate: precedence without brackets to show it would
make the display lie about what is pending.

Dividing by zero says so in words instead of printing `inf`.

Numbers are spoken up to **999,999**, composed from the same recorded word bank
the clock uses plus two new words — 407,209 is "four hundred and seven thousand
two hundred and nine", six recordings rather than one it could never have.
Decimals are read digit by digit after "point", the way people actually say
them. Past a million it shows the figure and says so rather than guessing.

### Hearing you in a noisy room

A fixed loudness threshold works in the room it was tuned in and nowhere else. A
quiet study sits near silence; a kitchen with an extractor fan can sit *above*
the old threshold all day, so every wake word was followed by "speech" that was
really just the fan.

The room now measures itself. While nobody is talking, the frames already going
to the recogniser are also used to track what the room sounds like at rest —
and speech has to stand out from **that**, not from a number chosen months ago
on a different desk. The estimate falls quickly and rises slowly, so somebody
talking is never learned as background.

Every listen logs all of it:

```
I (12345) recorder: Captured 2.31 s, 0.94 s of it speech (peak 8213, room 240, needed 1440)
```

**gear → VOICE → Room noise** has Automatic (right almost everywhere), Quiet
room and Noisy room. Each sets two things together: how far above the background
speech must be, and how sure the recogniser has to be before acting on its best
guess — because in a noisy room there is more to mishear as well as more to
mistake for talking.

### "It says *I did not hear anything* to everything I ask"

This was a real bug in v3.6, fixed in v3.7. Two separate thresholds were set
too high, and they stacked:

**The phrase floor was 0.35.** MultiNet does not report a calibrated
probability — a completely correct match routinely scores 0.14 to 0.30. So a
floor of 0.35 did not reject the occasional bad guess, it rejected almost every
command, including ones said clearly into a silent room. Every built-in phrase
fell through to the "treat it as a question" path.

**The speech gate was tested before everything else.** The gate exists to avoid
*paying* to transcribe a cough. Putting it above every other test meant a
cost-control device decided whether the buddy responded at all — so when it was
mis-tuned for the room, it silenced answers the chip would have given for free.

Three things changed, and the third matters most:

- The floor dropped to 0.10, and the real test for a wrong match is now the gap
  between the best phrase and the runner-up. That is a ratio, so it does not
  care what scale the model reports on.
- The room can no longer push the speech threshold above
  `BUDDY_SPEECH_THRESHOLD_CEILING`. Past that point the gate has stopped being
  useful, so it stops being an obstacle rather than silently becoming an
  absolute one.
- **Only an inaudible recording may silence the buddy.** Not the gate, not the
  noise profile, not the room estimate, not a missing API key — just a
  recording too short to hold a sentence, or one whose loudest single sample
  never reached `BUDDY_AUDIBLE_PEAK_AMPLITUDE`. Everything else gets an answer
  of some kind, even if that answer is "I do not know that one". A test
  sweeps every peak level against every mode to keep it that way.

Smart mode also stopped apologising when the cloud is unreachable; it answers
offline instead. Always-ask still declines, because there you asked for the
model's answer specifically.

**gear → Hearing** shows the room level, the current threshold and the peak of
the last thing you said, with a button to forget the room and measure again.
If the last number is far below the second, the buddy is hearing you but not
counting it as speech.

### "Asking it to sing crashes it, and it's always that one track"

It was never that track. Fixed in v3.7.1.

`audio_music_play_random()` picks a track at random, so whichever one came up
got the blame. What actually happened is that `voice_local_brain_answer()`
doesn't just *describe* what to do — it does it, so handling "sing me a song"
**starts the MP3 before returning**. The command handler then played the
"understood" chime at it.

That chime is not a notification. It is a blocking WAV that calls
`bsp_codec_set_fs()` and toggles mute — both of which reconfigure the I2S
channel — and then writes samples into that channel from the calling task,
while the player's own task is writing to it too. Reconfiguring a channel
underneath a task blocked inside a write on it is a reliable way to take an
ESP32 down, and two unsynchronised writers on one channel is the rest of it.

Worth being precise about what was *not* wrong, because the obvious explanation
is the wrong one: the sample rate never changed. `speaker_clock_cb` keeps the
codec at `BUDDY_AUDIO_SAMPLE_RATE_HZ` for everything and resamples in software
so the wake word keeps working, so a chime and a track were already at the same
rate. The hazard was the reconfiguration and the second writer, not the rate.

Three changes:

- No chime when the reply has already taken over the speaker. Starting the
  music *is* the acknowledgement. Stopping still chimes — nothing is playing
  after it, and that is the one case where silence needs confirming.
- `hw_speaker_play_wav_blocking()` refuses outright while the player is
  running, so every caller gets the guarantee rather than just the fixed one.
- Starting a track now stops whatever was playing first, and a `FILE` handle
  the player refuses is closed instead of leaked. SPIFFS is mounted with only a
  handful of file slots, so a few leaked handles made every later `fopen`
  return NULL — music and speech both went silent with nothing in the log.

`check_sources.py` now also walks every frame header of every `.mp3` in
`spiffs/music/` and reports truncation, stray bytes between frames, or a sample
rate that changes mid-track — the three ways a file can play fine on a computer
and still be wrong for this firmware.

### The music

Four pieces, all public domain, all rendered by this project's own synthesiser
from scores written out in `scripts/make_music.py`:

- **Ode to Joy** — Beethoven, Symphony No. 9, Op. 125 (1824)
- **Für Elise** — Beethoven, Bagatelle No. 25 in A minor, WoO 59
- **Eine kleine Nachtmusik** — Mozart, Serenade No. 13, K. 525 (1787)
- **Spring** — Vivaldi, The Four Seasons, Op. 8 No. 1 (1725)

(The Four Seasons are Vivaldi's rather than Mozart's — written some thirty years
before Mozart was born — so there is one of each.)

A composition falling out of copyright does not make a *recording* of it free —
performances carry their own rights, which is the trap behind most "royalty
free classical" downloads. Here both halves are settled: the compositions have
been public domain for over a century, and the recordings are generated from
scratch by a script in this repository, so they carry the same Unlicense/CC0 as
the rest of the project. Nothing is sampled and nothing was downloaded.

### It says the answers out loud, numbers included

Every reply the buddy knows by heart has a recording of itself on flash, found
by hashing the exact words it is about to show. That covers the jokes, the
facts, the greetings — anything with fixed wording.

It cannot cover the answers it works out. There is no recording of "it is 3:47
in the afternoon" and there never could be, because there are eighty-six
thousand of those. So those are assembled instead, from a recording per fragment
and a recording per number:

```
"It is"  +  "three"  +  "forty seven"  +  "in the afternoon."
```

The pieces are joined into one MP3 stream and decoded in a single pass, so it
comes out as a sentence rather than four clips with gaps between them. The time,
the date, the temperature, the humidity, the volume, the countdown and the
answer to a sum are all spoken this way, with no network and no cost.

A single missing fragment abandons the whole utterance rather than saying part
of it — "it is three in the afternoon" at a quarter to four would be
confidently wrong, and mute beats wrong. If you reword a fragment without
re-running `make_voice_clips.py`, that one answer goes quiet and says so in the
log.

### Sums

"What is two plus two" does not need a language model, so it does not use one.
`main/voice/voice_math.c` reads the question and works it out on the chip:
digits or words, `+ - * /`, "multiplied by", "divided by", "percent of", and
numbers up to a million.

That happens in two places. Offline, the sums registered in
`main/voice/voice_commands.c` are recognised directly — those are the ones
listed under **Ask it to do a sum** on the Question examples screen. Online, the
transcript is run through the same parser *before* the question would have gone
to the model, so any sum you can say is answered instantly and costs nothing.

Anything the parser cannot read — including dividing by zero — falls through
untouched, which is the right answer for a parser this small.

### Anything else

If what you said is not on that list, the recording goes to the online brain:
transcribed, answered, and read back aloud. The answer appears in the caption
bar at the bottom of the screen as it is spoken.

The buddy also asks the model to tag each reply with a mood, which is parsed off
and used to set the expression — so a surprising answer arrives with a surprised
face. It remembers the last few exchanges, so follow-up questions work.

---

## Why it cannot use Bluetooth for music

This comes up every time, so: the ESP32-S3 has **Wi-Fi and Bluetooth LE only**.
It has no Bluetooth Classic radio at all, and A2DP — the profile phones use to
stream audio — runs on Bluetooth Classic. It is a silicon limitation, not a
firmware one, and no amount of code can work around it.

So music comes from one of two places, chosen in Settings:

- **Flash** — drop `.mp3` files into `spiffs/music/` and rebuild. Instant, works
  with no network. The storage partition holds about 3.4 MB by default.
- **Wi-Fi stream** — give it a URL in Settings and it fetches and plays that
  instead. Point it at a file served by your computer, or any MP3 stream.

---

## How the code is organised

Every file does one thing and says so at the top. The folders map onto layers,
and dependencies only ever point downwards.

```
CMakeLists.txt              The project
partitions.csv              Flash layout
sdkconfig.defaults          Build configuration
components/bsp/             Board support: panel, codec, buttons, sensor dock
spiffs/                     Chimes, and your music in spiffs/music/
scripts/                    Helper scripts (see below)
tests/                      Tests that run on your computer, not the box

main/
├── main.c                  Start-up order, and nothing else
├── buddy_config.h          Every tunable number in the firmware
│
├── core/
│   └── buddy_settings      Everything the user can configure, saved in flash
│
├── hardware/               Talks to chips. Knows nothing about behaviour.
│   ├── hw_display          LCD, touch, and the LVGL lock
│   ├── hw_speaker          Playback, volume, and the loudness meter for lip sync
│   ├── hw_microphone       Reading the two-microphone array
│   ├── hw_sensors          Temperature, humidity, presence radar
│   └── hw_buttons          The mute switch
│
├── net/
│   ├── net_wifi            Joining a network, scanning for networks
│   └── net_time            Network time and time zones
│
├── audio/
│   ├── audio_prompts       The chirps and buzzes
│   └── audio_music         Playlist, shuffle, and streaming
│
├── face/                   Draws the face. Knows nothing about voice.
│   ├── face_theme.h        The colour palette
│   ├── face_pose           What an expression IS: a struct of numbers
│   ├── face_emotions       The table of expressions and idle antics
│   ├── face_view           Turning a pose into LVGL widgets
│   └── face_animator       Deciding when the face changes
│
├── ui/                     Screens.
│   ├── ui_screen_face      The home screen, and the lock-taking wrappers
│   ├── ui_sensor_readout   The temperature chip in the top right
│   ├── ui_speech_bubble    The caption bar along the bottom
│   └── ui_settings         Everything behind the gear
│
└── voice/                  Listening and answering.
    ├── voice_commands      The offline vocabulary — a table you can edit
    ├── voice_wakeword      WakeNet and MultiNet, behind one callback
    ├── voice_recorder      Capturing a question into PSRAM as a WAV
    ├── voice_local_brain   Offline answers
    ├── voice_cloud_brain   Transcribe, answer, speak
    └── voice_assistant     The conductor: what happens next, and when
```

**If you read one file, read `main/voice/voice_assistant.c`.** The conversation
flow lives there and nowhere else.

**If you want to change how it looks, read `main/face/face_pose.h`.** It explains
the one idea the whole face system is built on: an expression is just a set of
numbers, so animating between two expressions is arithmetic, and adding a new
expression is a row in a table.

### Scripts

| Script | What it does |
| --- | --- |
| `scripts\run_tests.py` | Builds and runs the host test suite. Start here. |
| `scripts\check_sources.py` | Static sanity check: functions declared but never defined, calls to things that do not exist, sources missing from the build. |
| `scripts\tidy_project_folder.ps1` | Moves the leftover esp-box repository files into `_archive\`, leaving a clean standalone project. Deletes nothing. |
| `scripts\make_chimes.py` | Regenerates the four notification sounds in `spiffs/`. Edit the note table at the bottom to change them. |

---

## Making it yours

| Want to... | Edit |
| --- | --- |
| change the red, the white, the blush | `main/face/face_theme.h` |
| make it blink more, fidget more, look further | `main/buddy_config.h` |
| add an expression | `main/face/face_emotions.h` and `.c` |
| add an idle antic | the step arrays in `main/face/face_emotions.c` |
| add a voice command | `main/voice/voice_commands.c`, then handle it in `main/voice/voice_local_brain.c` |
| change the jokes and greetings | the tables at the top of `main/voice/voice_local_brain.c` |
| change its personality | Settings on the device, or the default in `main/core/buddy_settings.c` |
| change the chimes | `scripts/make_chimes.py`, then rerun it |
| move the face, resize the eyes | the geometry section of `main/buddy_config.h` |
| make the whole face bigger or smaller | `FACE_SCALE_PERCENT` in `main/buddy_config.h` |
| teach it another offline sum | add a row to `main/voice/voice_commands.c` |

### Making the face bigger or smaller

One number does it:

```c
/* main/buddy_config.h */
#define FACE_SCALE_PERCENT              86
```

Every pixel dimension of the face — eyes, brows, mouth, blush, and each
expression's tweaks to them — is written at design size and passed through
`FACE_SCALED()`. Change the percentage and the whole face resizes in
proportion; nothing has to be re-tuned by hand.

100 is the original size, which is big enough that a raised eyebrow overlaps the
temperature pill in the top-right corner. 86 is the current setting. Below about
70 the face starts to look lost on the screen.

Two things are deliberately *not* scaled. The `*_CENTRE_Y` values say where the
face sits, which is a separate decision from how big it is. And
`FACE_BROW_MIN_BASELINE_Y` is a hard floor that stops a raised brow climbing
into the sensor pill no matter what else is going on — `no_expression_lets_the_face_reach_the_sensor_pill`
in the host tests checks every expression against it.

### Adding an expression, start to finish

1. Add a name to `face_emotion_t` in `main/face/face_emotions.h`.
2. Add a row to `s_emotion_poses` in `main/face/face_emotions.c`. The comment
   above the table explains what each number does.
3. Add the lower-case name to `s_emotion_names` so the online brain can request
   it by mood tag.

That is all. No drawing code changes.

---

## Testing

There are two layers, and between them they cover the things that actually go
wrong with a project like this.

### 1. Host tests — run before you build

```powershell
python scripts\run_tests.py
```

Around 13,000 checks, in under a second, with no hardware involved. It compiles
the pure-logic parts of the firmware with an ordinary C compiler and checks
them over.

Most of it is aimed at the **hand-written tables**, because that is where the
bugs really live. Seventeen expressions, twelve antic scripts and sixty-odd
voice phrases were all typed out by hand, and a single transposed number there
gives you a face that looks subtly wrong or a phrase the recogniser silently
refuses to load — with nothing in any log to tell you why.

| Group | What it proves |
| --- | --- |
| Pose blending | The interpolation every animation is built on: endpoints exact, midpoints between, booleans flipping at halfway, NULLs survived. |
| The emotion table | Every mood has a unique name that round-trips; every pose is geometrically drawable (no zero-area eyes, no lids deeper than the eye, nothing off-canvas); the neutral face smiles, the sad face frowns, the wink closes one eye, the speaking face starts with its mouth open. |
| The antic scripts | Every step names a mood that exists, gaze values are percentages, transitions are slow enough to see, each antic lasts between 0.3 and 6 seconds, and every one ends with the face settled back to centre. |
| The vocabulary | Every phrase is something MultiNet will accept (lowercase, 2–7 words), the English text and phonetic columns agree, no phrase is duplicated within a language, and **every action the code can produce has a phrase that triggers it**. |
| Screen layout | No expression, at any gaze or antic offset, can put an eyebrow inside the temperature pill; the eyes, mouth and blush all stay on a 320x240 screen; every scaled dimension survives the integer division rather than collapsing to zero; and the tallest caption bubble still leaves the lifted eyes above it. |
| Sample rate conversion | A second of 44.1 kHz audio comes out as the right number of 16 kHz frames with no drift; a 1 kHz tone is still 1 kHz afterwards rather than the 2,756 Hz you get by skipping the conversion; the converter respects the room it was given, survives NULLs and empty buffers and mono, clamps a nonsense channel count, and leaves no step in the waveform at buffer seams. |
| Offline arithmetic | Sums in digits and in words, every wording of every operator, non-whole answers formatted the way a person would say them, and — importantly — the questions that are *not* sums being left alone for the cloud. Plus NULL input, a zero-length buffer, a 4 KB question and a line of nothing but operators. Every registered sum phrase is checked to be one the parser can actually read. |
| The examples screen | Every action has a title, none is left on the fallback, and every titled group has at least one phrase to list underneath it — so the screen can never show an empty heading. |
| Mood tags | Parsing replies from a remote model that may misbehave: valid tags, unknown moods, unclosed brackets, empty brackets, leading newlines, empty and NULL input — and in every failure case the answer text survives intact. |
| The kitchen timer | A five-minute countdown reaching zero exactly once and never firing again; a thirty-minute one not drifting; cancelling, restarting and refusing nonsense durations; every registered phrase naming the duration a person would expect from reading it; and the badge and the spoken remainder both formatted the way each is read. The clock is a variable the tests move, so half an hour is verified in microseconds. |

These are not decorative. Shrinking the face broke three of them immediately:
two expressions had a mouth opening wider than the new maximum, and the yawn's
eyelids — still written at the old size — grew deep enough to erase the eye
entirely. All three would have shipped as a face that just looked wrong.

The suite was verified by deliberately breaking things: inverting the happy
face's smile, duplicating a phrase, introducing an off-by-one in the tag
parser, and pointing an antic at a mood that does not exist. Each produced a
named, specific failure.

If you do not have a desktop C compiler, the script says so and tells you how
to get one. It is not required to build the firmware.

### 2. The on-device self test — runs at every boot

The buddy checks itself once everything is up, and prints a block like this to
the serial monitor:

```
---- self test ----------------------------------
  [ok  ] PSRAM          6284 KB free
  [ok  ] Internal RAM   118 KB free
  [ok  ] Filesystem     mounted at /spiffs
  [ok  ] Chimes         all 4 present
  [note] Music          no tracks in /spiffs/music
  [ok  ] Sensor dock    23.4 C, 41% humidity
  [ok  ] Speech engine  listening, 38 phrases
  [ok  ] Microphone     live
  [note] Wi-Fi          not set up yet
  [note] Online brain   switched off
---- 7 ok, 3 notes, 0 failed ------------------
```

`note` means usable but something is missing or optional. `FAIL` means a
feature genuinely will not work, and the detail says which.

You can read the same report on the box itself: **gear → Run diagnostics**.
That is the fastest way to answer "why is it not doing X" without plugging it
into a computer.

**Watch the Internal RAM line.** It is the one that actually bites, because
PSRAM cannot stand in for it — DMA buffers and the I2C driver's little command
descriptors have to live in the 320 KB of internal SRAM or not at all. When it
runs dry you do not get a tidy out-of-memory message, you get this:

```
E (36960) i2c: i2c_master_start(1300): i2c command link error
E (36961) i2c: i2c_master_write_byte(1339): i2c command link error
...
assert failed: aht20_read_reg aht20.c:54 (ESP_OK == ret)
```

once a second, ending in a reboot. `i2c_cmd_link_create()` returned NULL, the
temperature read failed, and the AHT20 driver asserts that its reads succeed.
The fix is never in the I2C code — it is to give internal RAM back. The two
biggest levers, in order:

| Lever | Where | Cost |
|---|---|---|
| `CONFIG_BSP_LCD_DRAW_BUF_HEIGHT` | `sdkconfig.defaults` | 320 × height × 2 bytes per buffer |
| `.double_buffer` | `main/hardware/hw_display.c` | doubles the above |

At 40 lines double-buffered that is 50 KB and the box crashes; at 20 lines
single-buffered it is 12.5 KB and it does not. Redraws are a shade less silky
at the smaller setting, which is the trade.

---

## "It stops hearing me while music is playing"

It used to, and the reason is worth knowing if you ever add audio features.

The ES8311 speaker and the ES7210 microphone array are wired to the **same I2S
port**, and an I2S port has exactly **one clock**. Setting the speaker to
44.1 kHz for a music file therefore sets the microphone to 44.1 kHz as well —
visible in the log the moment a song starts:

```
I2S_IF: STD Mode 1 bits:16/16 channel:2 sample_rate:44100   <- speaker
I2S_IF: STD Mode 0 bits:16/16 channel:2 sample_rate:44100   <- microphone
```

Nothing tells WakeNet the rate moved. It is a 16 kHz model and it carries on
taking 16000 samples a second out of a stream now arriving at 44100, so every
voice it hears is 2.76× too fast and nearly two octaves too high. "Hi E.S.P."
cannot match — including, infuriatingly, when what you want to say is "stop the
music".

So the codec clock never moves any more. `main/audio/audio_resampler.c`
converts higher-rate audio down to 16 kHz in software on its way to the
speaker, and `main/hardware/hw_speaker.c` sets the codec to 16 kHz no matter
what a track asks for. The cost is everything above 8 kHz; on a speaker this
size that was mostly theoretical.

Two consequences worth remembering:

* **Put 16 kHz files in `spiffs/music/`.** They play with no conversion at all.
  `ffmpeg -i yours.mp3 -ar 16000 -b:a 64k ready.mp3`
* **Never call `bsp_codec_set_fs()` with anything but `BUDDY_AUDIO_SAMPLE_RATE_HZ`.**
  That one line is the whole bug, and it is a silent one — the buddy looks
  perfectly alive and simply never answers again.

If it still struggles to hear you over a song, that is a different problem:
acoustic echo, not sample rate. Acoustic echo cancellation is off in this build,
so the microphone hears the music at full strength. Turn the volume down and it
comes back.

---

## access() does not work on SPIFFS

Worth knowing before you write any "is this file there?" check in this
firmware, because the failure is silent and total.

ESP-IDF routes `access()` through its VFS layer to a per-filesystem handler,
and **the SPIFFS driver does not register one**. Every call fails with `ENOSYS`
regardless of what is actually on flash — which is indistinguishable from the
file being missing. It cost a long time to find: the boot log insisted

```
W (1560) prompts: Missing chime: /spiffs/chime_wake.wav
I (1561) prompts: 0 of 4 chimes available
```

on a board whose storage partition demonstrably held all four, alongside
`voice_clips: 47 spoken replies on flash` from an `opendir()` scan of the very
same filesystem. `opendir` and `readdir` are implemented; `access` is not.

Use `fopen()` and close it again, as `audio_prompts.c` and `audio_voice.c` now
do. `stat()` also works. `access()` never will.

---

## Why the mouth used to look wrong

Worth writing down, because the cause was nowhere near where the problem showed.

The mouth is two objects: a curved polyline for a closed mouth, and a filled
pill for an open one. The hand-over between them used to be a CROSS-FADE — for
the whole middle of the opening range both shapes were drawn at partial
opacity. Where they overlapped the red doubled up; where they did not it stayed
pale. So the mouth wore a ghost of itself.

Worse, the two were anchored differently. The pill sat at `curve / 3` while the
line's low point was at `curve`, so the ghost was offset rather than
concentric — and lip sync sweeps through that band about eighteen times a
second. The result was a face that looked subtly, unplaceably wrong.

It is now a single clean switch at `MOUTH_BECOMES_OPENING_AT`, set to the
stroke thickness so that at the moment of the swap the two shapes are the same
size and the change cannot be seen. Neither is ever alpha-blended now, which is
also the cheapest thing LVGL can draw — the fix made the face faster as a side
effect.

---

## Where the speed went

Three changes, none of which cost a byte of RAM:

* **No alpha blending in the mouth.** See above. Translucent shapes are the
  expensive path in LVGL's software renderer.
* **The blush is hidden, not transparent.** An invisible object still gets
  repositioned every frame, and every reposition invalidates the area it left
  AND the area it arrived in. Most expressions have no blush, so that was two
  pointless redraw regions on nearly every frame of every animation.
* **Unchanged poses do nothing at all.** Animations tick at the display refresh
  rate whether or not the numbers they produce have moved, and blending two
  poses that are already close yields the same rounded integers several frames
  running. `face_view_apply_pose()` now compares against the last pose applied
  and returns early — on a face that is mostly sitting still, that is most of
  the frames.

One change that did cost RAM: the draw strip went from 20 lines to 30 (12.5 KB
to 19.2 KB), which is a third fewer round trips through the SPI bus for a
full-screen redraw. That is affordable now only because the earlier
double-buffered 40-line setting — the one that crashed the box — is long gone.

**Flash is not the constraint and never was.** The app uses 44% of its
partition and the storage partition is under a third full. Internal RAM is the
scarce resource on this board; everything above is about redraw work, not
space. The voice clips did drop from 596 KB to 492 KB by moving to 24 kbps,
which is inaudible at this speaker size, but that was tidying rather than
necessity.

---

## "It says Connecting and never connects"

Two separate bugs produced that, and either one alone was enough.

**The screen could not change its mind.** The Wi-Fi page set the label to
"Connecting to X..." once, when the password was entered, and then had no route
to ever update it. `net_wifi.c` has a single state callback and `main.c` had
already claimed it to start the clock, so nothing on that page ever heard about
the outcome. The label sat at "Connecting..." whether the attempt failed **or
succeeded** — from the outside, indistinguishable, and both look like a loop.
The page now polls the live state twice a second and owns the timer, so it dies
with the page.

**Nothing timed the attempt out.** `BUDDY_WIFI_CONNECT_TIMEOUT_S` had been
defined since the first version and was never once referenced. That matters
because joining a network is TWO steps that fail differently:

| What happened | What you see | What to fix |
|---|---|---|
| Association refused | a disconnect event, with a reason code | password, or the band |
| Associated, no DHCP lease | **nothing at all** | the router |

The old state machine only counted disconnect events. A router that accepted
the password and then never handed out an address produced no events, so
nothing decremented, nothing failed, and it waited for ever. There is a
deadline on the whole business now, and it reports which half stalled — the
screen says either "Connecting to X..." or "Joined X. Waiting for an
address...", which are completely different problems.

Three more things were wrong in the same file:

* **A `vTaskDelay()` sat in the disconnect handler.** That runs on the system
  event task — the same task that delivers `IP_EVENT_STA_GOT_IP`. Sleeping a
  second there stops the address being delivered while it sleeps and backs
  every other event up behind it. Retries are scheduled on a timer now.
* **A deliberate disconnect counted as a failure.** Switching networks tears
  the old association down first, and `esp_wifi_disconnect()` is asynchronous —
  the event lands in the handler that counts failures. Every attempt spent one
  of its own retries on the teardown it had asked for, and raced a second
  `esp_wifi_connect()` against the first. The loser reported a failure
  indistinguishable from a wrong password.
* **`WIFI_FAST_SCAN`** took whichever matching access point answered first,
  which on a mesh network can be the one with the worst signal in the house.
  Now it scans every channel and picks the strongest.

**If it still will not join, check the band first.** The ESP32-S3 has no 5 GHz
radio at all, so a 5 GHz-only network is invisible to it however strong the
signal is on your phone. Anything the scan lists is 2.4 GHz by definition — a
network missing from that list is almost never out of range.

---

## The two voices

The buddy speaks with whichever voice it can reach, best first.

**Online.** OpenAI text-to-speech. It can say anything — the time, a
temperature, the answer to a question it has never seen — and it sounds like a
person. Needs Wi-Fi, an API key, and the **Answer any question** switch on.

**Offline.** A folder of small recorded clips in the storage partition, one per
answer the buddy knows by heart. No network, no account, no cost. Every reply
still appears in the speech bubble either way; this is what plays instead of
silence when there is nothing to reach.

Each clip is named after a hash of the exact words it speaks:

```
"Oh hey, you are back."  ->  /spiffs/voice/5b489fae.mp3
```

so neither side keeps a list and neither can quietly drift. Reword a reply and
its hash changes, the lookup misses, and that one line goes back to being
caption-only — never to the buddy confidently saying the previous version.

Replies assembled at runtime — the time, the temperature, a track name, the
answer to a sum — have no fixed text to hash and so have no clip. Those are
spoken only by the online voice. Covering them offline would mean stitching
word-by-word clips together, which sounds worse than either option and costs
several times the flash.

**After changing any reply in `voice_local_brain.c`, re-render the clips:**

```powershell
python scripts\make_voice_clips.py
```

It needs `espeak-ng` and `ffmpeg` on your PATH, reads the reply strings straight
out of the source, and rewrites `spiffs/voice/`. Forty-seven clips come to about
450 KB. The boot self-test reports how many it found; if that line says zero,
this is the script you did not run.

---

## Notes on the hardware

**The sensor dock is optional.** Without it the readout hides itself and the
temperature questions say so politely. Everything else works.

**The mute switch on the top edge** cuts the microphone. The buddy keeps its
face and keeps playing music, it just stops listening.

**Memory.** The recording buffer, streamed music and TLS all live in external
PSRAM. The speech models need internal RAM, which is why the display buffer is
kept small and Bluetooth is switched off entirely.

**Flash.** Dropping ESP RainMaker from the stock demo freed a large slice of the
application partition. If you want more room for music, take it from `ota_0` in
`partitions.csv` — check the size printed at the end of a build first.
