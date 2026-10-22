"""
make_music.py — renders the buddy's music from public-domain scores.

WHY THIS REPLACED THE GENERATED JINGLE
--------------------------------------
The box used to ship "short-tune", an original melody written in this script's
predecessor. That was license-clean but it was also nobody's idea of a tune —
it existed to prove the audio path worked. What people actually want a singing
desk toy to sing is something they recognise.

So it now plays real music, and the licensing is settled twice over:

  * The COMPOSITIONS are public domain. The newest of them was written in 1787
    and the oldest in 1725, so all four have been free of copyright everywhere
    for well over a century.

  * The RECORDINGS are made here, from scratch, by the synthesiser below. A
    performance can carry its own copyright even when the composition does not,
    which is the trap that catches most "royalty free classical" downloads.
    Nothing is sampled, nothing is downloaded, and the output of this script is
    covered by the same Unlicense/CC0 as the rest of the project.

That means the whole audio chain — score, performance, encoder — is something
the project owns outright and can be redistributed by anybody.

THE PIECES
----------
  Ode to Joy       Beethoven, Symphony No. 9 in D minor, Op. 125, fourth
                   movement (1824). Transposed to F major to sit in the range
                   where this synthesiser sounds best.

  Fur Elise        Beethoven, Bagatelle No. 25 in A minor, WoO 59 (composed
                   1810, published 1867). The opening section only.

  Eine kleine      Mozart, Serenade No. 13 in G major, K. 525 (1787). The
  Nachtmusik       opening of the first movement.

  Spring           Vivaldi, The Four Seasons, Op. 8 No. 1 (1725). The opening
                   ritornello, transposed to G.

USAGE
-----
    pip install numpy
    sudo apt-get install ffmpeg
    python3 scripts/make_music.py

Writes straight into spiffs/music/, replacing what is there.
"""

import os
import subprocess
import sys
import wave

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
PROJECT = os.path.dirname(HERE)
OUTPUT = os.path.join(PROJECT, "spiffs", "music")

SAMPLE_RATE = 44100

# 48 kbps stereo. Down from 64 when the playlist went from two pieces to four.
#
# The storage partition is shared with 356 voice recordings, and SPIFFS wants
# free blocks to write into — a filesystem past about 85 percent gets slow and
# then starts failing. On a speaker this size the step from 64 to 48 is not
# audible; the third of a megabyte it returns is what keeps the image healthy.
# The size report at the end of a run is the number to watch.
BITRATE = "48k"

rng = np.random.default_rng(20260903)


# ---------------------------------------------------------------------------
# The synthesiser
#
# Carried over from make_short_tune.py, which explains the reasoning behind
# each choice at length. The short version: a pure sine sounds like a test
# tone, perfectly quantised notes sound like a typewriter, and notes that stop
# dead sound like a computer. Every one of those is worked against here.
# ---------------------------------------------------------------------------

def hz(name):
    """Frequency of a note written like 'A4', 'C#5', 'Eb3'."""
    steps = {"C": 0, "D": 2, "E": 4, "F": 5, "G": 7, "A": 9, "B": 11}
    semitone = steps[name[0]]
    index = 1
    while index < len(name) and name[index] in "#b":
        semitone += 1 if name[index] == "#" else -1
        index += 1
    octave = int(name[index:])
    return 440.0 * 2.0 ** ((semitone + 12 * (octave - 4) - 9) / 12.0)


def envelope(length, attack, decay, sustain, release):
    """A four-stage volume shape, in samples."""
    out = np.zeros(length)
    a = min(int(attack * SAMPLE_RATE), length)
    d = min(int(decay * SAMPLE_RATE), length - a)
    r = min(int(release * SAMPLE_RATE), length - a - d)
    s = length - a - d - r

    cursor = 0
    if a:
        out[cursor:cursor + a] = np.sin(np.linspace(0, np.pi / 2, a)) ** 2
        cursor += a
    if d:
        out[cursor:cursor + d] = np.linspace(1.0, sustain, d)
        cursor += d
    if s > 0:
        out[cursor:cursor + s] = sustain
        cursor += s
    if r:
        out[cursor:cursor + r] = np.linspace(sustain, 0.0, r)
    return out


def voice(freq, seconds, harmonics, attack, decay, sustain, release,
          vibrato_hz=0.0, vibrato_cents=0.0, detune_cents=0.0):
    """One note. harmonics is a list of (multiple, level, extra_damping)."""
    length = int(seconds * SAMPLE_RATE)
    t = np.arange(length) / SAMPLE_RATE
    out = np.zeros(length)

    drift = np.zeros(length)
    if vibrato_cents:
        depth = vibrato_cents * np.clip(t / 0.35, 0.0, 1.0)
        drift = depth * np.sin(2 * np.pi * vibrato_hz * t)

    for cents in ({0.0, detune_cents} if detune_cents else {0.0}):
        base = freq * 2.0 ** ((cents + drift) / 1200.0)
        phase = 2 * np.pi * np.cumsum(base) / SAMPLE_RATE
        phase += rng.uniform(0, 2 * np.pi)
        for multiple, level, damping in harmonics:
            out += level * np.exp(-damping * t) * np.sin(multiple * phase)

    return out * envelope(length, attack, decay, sustain, release)


# A struck-string voice for the melody: bright on the attack, mellow as it
# rings. Close enough to a celesta to suit a small speaker.
BELL = dict(
    harmonics=[(1, 1.00, 0.6), (2, 0.34, 1.5), (3, 0.15, 2.6), (4, 0.07, 4.0)],
    attack=0.012, decay=0.30, sustain=0.42, release=0.55,
    vibrato_hz=4.8, vibrato_cents=6.0, detune_cents=5.0,
)
BASS = dict(
    harmonics=[(1, 1.00, 0.9), (2, 0.28, 2.0), (3, 0.08, 3.4)],
    attack=0.010, decay=0.22, sustain=0.30, release=0.40,
)
PAD = dict(
    harmonics=[(1, 0.55, 0.10), (2, 0.16, 0.30), (3, 0.07, 0.55), (5, 0.03, 0.9)],
    attack=0.45, decay=0.40, sustain=0.72, release=0.90,
    vibrato_hz=3.1, vibrato_cents=4.0, detune_cents=7.0,
)


def place(track, start_seconds, samples):
    """Mix samples into track at a start time, clipping at the ends."""
    start = max(0, int(start_seconds * SAMPLE_RATE))
    end = min(start + len(samples), len(track))
    if end > start:
        track[start:end] += samples[:end - start]


def reverb(mono, room=0.62, mix=0.30, repeats=18):
    """Four feedback delay lines at mutually prime spacings — a small room."""
    wet = np.zeros_like(mono)

    for delay_ms, gain in ((29.7, 0.80), (37.1, 0.76), (41.1, 0.72), (43.7, 0.68)):
        delay = int(delay_ms * SAMPLE_RATE / 1000.0)
        line = np.zeros_like(mono)
        level = 1.0
        for repeat in range(1, repeats + 1):
            level *= room * gain
            if level < 1e-4:
                break
            offset = delay * repeat
            if offset >= len(mono):
                break
            line[offset:] += level * mono[:len(mono) - offset]
        wet += line / 4.0

    return (1.0 - mix) * mono + mix * wet


def soft_clip(x):
    return np.tanh(1.4 * x) / np.tanh(1.4)


# ---------------------------------------------------------------------------
# Ode to Joy — Beethoven, Symphony No. 9, Op. 125 (1824)
#
# Written here in F major rather than the original D. The melody is the same
# tune either way, and F puts it in the octave where the bell voice above has
# the most body without getting shrill on a speaker this size.
#
# Notation is (note, beats); None is a rest. The two dotted-quarter/eighth
# figures at the end of each half are what stop it marching.
# ---------------------------------------------------------------------------

ODE_MELODY = [
    # Phrase one
    ("A5", 1), ("A5", 1), ("Bb5", 1), ("C6", 1),
    ("C6", 1), ("Bb5", 1), ("A5", 1), ("G5", 1),
    ("F5", 1), ("F5", 1), ("G5", 1), ("A5", 1),
    ("A5", 1.5), ("G5", 0.5), ("G5", 2), (None, 0),

    # Phrase two — same opening, resolving home instead of hanging
    ("A5", 1), ("A5", 1), ("Bb5", 1), ("C6", 1),
    ("C6", 1), ("Bb5", 1), ("A5", 1), ("G5", 1),
    ("F5", 1), ("F5", 1), ("G5", 1), ("A5", 1),
    ("G5", 1.5), ("F5", 0.5), ("F5", 2), (None, 0),

    # The middle, which steps down where the others step up
    ("G5", 1), ("G5", 1), ("A5", 1), ("F5", 1),
    ("G5", 1), ("A5", 0.5), ("Bb5", 0.5), ("A5", 1), ("F5", 1),
    ("G5", 1), ("A5", 0.5), ("Bb5", 0.5), ("A5", 1), ("G5", 1),
    ("F5", 1), ("G5", 1), ("C5", 2),

    # And the opening again to close
    ("A5", 1), ("A5", 1), ("Bb5", 1), ("C6", 1),
    ("C6", 1), ("Bb5", 1), ("A5", 1), ("G5", 1),
    ("F5", 1), ("F5", 1), ("G5", 1), ("A5", 1),
    ("G5", 1.5), ("F5", 0.5), ("F5", 2),
]

# One chord per bar, in the harmonisation everyone knows.
ODE_CHORDS = [
    ("F2", ["F3", "A3", "C4"]), ("C3", ["E3", "G3", "C4"]),
    ("F2", ["F3", "A3", "C4"]), ("C3", ["E3", "G3", "C4"]),
    ("F2", ["F3", "A3", "C4"]), ("C3", ["E3", "G3", "C4"]),
    ("F2", ["F3", "A3", "C4"]), ("F2", ["F3", "A3", "C4"]),
    ("F2", ["F3", "A3", "C4"]), ("Bb2", ["Bb3", "D4", "F4"]),
    ("F2", ["F3", "A3", "C4"]), ("C3", ["E3", "G3", "C4"]),
    ("F2", ["F3", "A3", "C4"]), ("C3", ["E3", "G3", "C4"]),
    ("F2", ["F3", "A3", "C4"]), ("F2", ["F3", "A3", "C4"]),
]

# ---------------------------------------------------------------------------
# Fur Elise — Beethoven, Bagatelle No. 25 in A minor, WoO 59
#
# The opening section, which is the part anybody can hum. In 3/8, so a "beat"
# here is an eighth note and the tempo below is set accordingly.
# ---------------------------------------------------------------------------

ELISE_MELODY = [
    ("E5", 1), ("D#5", 1), ("E5", 1), ("D#5", 1), ("E5", 1), ("B4", 1),
    ("D5", 1), ("C5", 1), ("A4", 3),
    ("C4", 1), ("E4", 1), ("A4", 1), ("B4", 3),
    ("E4", 1), ("G#4", 1), ("B4", 1), ("C5", 3),

    ("E4", 1), ("E5", 1), ("D#5", 1), ("E5", 1), ("D#5", 1), ("E5", 1),
    ("B4", 1), ("D5", 1), ("C5", 1), ("A4", 3),
    ("C4", 1), ("E4", 1), ("A4", 1), ("B4", 3),
    ("E4", 1), ("C5", 1), ("B4", 1), ("A4", 4),
]

# A minor and E major alternating underneath, one per three beats.
ELISE_CHORDS = [
    ("A2", ["A3", "C4", "E4"]), ("E2", ["E3", "G#3", "B3"]),
    ("A2", ["A3", "C4", "E4"]), ("E2", ["E3", "G#3", "B3"]),
    ("A2", ["A3", "C4", "E4"]), ("E2", ["E3", "G#3", "B3"]),
    ("A2", ["A3", "C4", "E4"]), ("A2", ["A3", "C4", "E4"]),
]


# ---------------------------------------------------------------------------
# Eine kleine Nachtmusik — Mozart, Serenade No. 13 in G major, K. 525 (1787)
#
# The opening of the first movement: a rising call, answered a fifth away, then
# the scale that runs back down. In G, as written.
# ---------------------------------------------------------------------------

NACHTMUSIK_MELODY = [
    # The call, on the tonic
    ("G4", 1), ("D4", 1), ("G4", 0.5), ("D4", 0.5), ("G4", 0.5), ("B4", 0.5),
    ("D5", 2), (None, 2),

    # The answer, on the dominant
    ("D5", 1), ("A4", 1), ("D5", 0.5), ("A4", 0.5), ("D5", 0.5), ("F#4", 0.5),
    ("A4", 2), (None, 2),

    # And the run back down that everybody hums
    ("A4", 0.5), ("C5", 0.5), ("B4", 0.5), ("A4", 0.5),
    ("G4", 0.5), ("A4", 0.5), ("B4", 0.5), ("C5", 0.5),
    ("D5", 1), ("G4", 1), ("G4", 2),

    # Repeat of the call, closing home
    ("G4", 1), ("D4", 1), ("G4", 0.5), ("D4", 0.5), ("G4", 0.5), ("B4", 0.5),
    ("D5", 1), ("B4", 1), ("G4", 2),
]

NACHTMUSIK_CHORDS = [
    ("G2", ["G3", "B3", "D4"]), ("G2", ["G3", "B3", "D4"]),
    ("D2", ["D3", "F#3", "A3"]), ("D2", ["D3", "F#3", "A3"]),
    ("G2", ["G3", "B3", "D4"]), ("D2", ["D3", "F#3", "A3"]),
    ("G2", ["G3", "B3", "D4"]), ("G2", ["G3", "B3", "D4"]),
]

# ---------------------------------------------------------------------------
# Spring — Vivaldi, The Four Seasons, Op. 8 No. 1 (1725)
#
# Worth a note on the attribution: the Four Seasons are Vivaldi's, not
# Mozart's — they were written some thirty years before Mozart was born. Since
# the request was for one of the seasons AND for Mozart, both are here: the
# Nachtmusik above is the Mozart, and this is the season.
#
# The opening ritornello, transposed from E to G to sit beside the other
# pieces and stay in the synthesiser's best octave.
# ---------------------------------------------------------------------------

SPRING_MELODY = [
    ("G5", 0.5), ("G5", 0.5), ("G5", 1), ("G5", 0.5), ("G5", 0.5), ("G5", 1),
    ("G5", 0.5), ("B5", 0.5), ("D6", 1), ("D6", 0.5), ("C6", 0.5), ("B5", 1),

    ("A5", 0.5), ("A5", 0.5), ("A5", 1), ("A5", 0.5), ("A5", 0.5), ("A5", 1),
    ("A5", 0.5), ("C6", 0.5), ("B5", 1), ("A5", 0.5), ("G5", 0.5), ("F#5", 1),

    ("G5", 0.5), ("A5", 0.5), ("B5", 1), ("C6", 0.5), ("B5", 0.5), ("A5", 1),
    ("B5", 0.5), ("A5", 0.5), ("G5", 1), ("D5", 1), ("G5", 2),
]

SPRING_CHORDS = [
    ("G2", ["G3", "B3", "D4"]), ("G2", ["G3", "B3", "D4"]),
    ("D2", ["D3", "F#3", "A3"]), ("D2", ["D3", "F#3", "A3"]),
    ("G2", ["G3", "B3", "D4"]), ("C3", ["C4", "E4", "G4"]),
    ("D2", ["D3", "F#3", "A3"]), ("G2", ["G3", "B3", "D4"]),
]


def render_piece(melody, chords, bpm, beats_per_bar, repeats, melody_level):
    """Lay a melody over its chords, twice through, and return a stereo mix."""
    beat_seconds = 60.0 / bpm

    one_pass = sum(beats for _note, beats in melody)
    total_beats = one_pass * repeats
    total_seconds = total_beats * beat_seconds + 4.0

    lead = np.zeros(int(total_seconds * SAMPLE_RATE))
    under = np.zeros_like(lead)

    # --- melody -----------------------------------------------------------
    for pass_index in range(repeats):
        beat = pass_index * one_pass

        for note, beats in melody:
            if note is not None and beats > 0:
                # Lean on the downbeats and let the rest breathe; nudge every
                # note a few milliseconds off the grid.
                on_beat = abs(beat % beats_per_bar) < 0.01
                level = (0.98 if on_beat else 0.82) * rng.uniform(0.93, 1.07)
                jitter = rng.normal(0, 0.008)

                # Quieter on the first pass, so the repeat feels like it grows.
                pass_level = 0.85 if pass_index == 0 else 1.0

                place(lead, beat * beat_seconds + jitter,
                      voice(hz(note), beats * beat_seconds + 0.5, **BELL)
                      * melody_level * level * pass_level)
            beat += beats

    # --- bass and pad -----------------------------------------------------
    bar_count = int(np.ceil(total_beats / beats_per_bar))

    for bar in range(bar_count):
        beat0 = bar * beats_per_bar
        root, chord = chords[bar % len(chords)]
        last_bar = bar == bar_count - 1

        length = 6.0 * beat_seconds if last_bar else (beats_per_bar + 0.4) * beat_seconds

        place(under, beat0 * beat_seconds + rng.normal(0, 0.006),
              voice(hz(root), length, **BASS) * 0.26)

        for note in chord:
            place(under, beat0 * beat_seconds + rng.normal(0, 0.010),
                  voice(hz(note), length, **PAD) * 0.070)

    # --- mix --------------------------------------------------------------
    left = lead * 0.46 + under * 0.54
    right = lead * 0.54 + under * 0.46

    left = reverb(left)
    right = reverb(right, room=0.36)

    stereo = np.stack([left, right], axis=1)
    stereo /= np.max(np.abs(stereo)) + 1e-9
    stereo = soft_clip(stereo * 0.86)

    fade = int(0.12 * SAMPLE_RATE)
    stereo[:fade] *= np.linspace(0, 1, fade)[:, None]
    tail = int(2.0 * SAMPLE_RATE)
    stereo[-tail:] *= np.linspace(1, 0, tail)[:, None] ** 1.5

    return stereo


# SPIFFS stores an object name in a fixed field, 32 bytes including the
# terminator — so a path may be 31 characters. "/music/" eats seven of them.
#
# Getting this wrong does not fail here, it fails much later inside
# spiffsgen.py while the firmware image is being built, with a message about an
# object name rather than about the file you just added. Checking at the point
# the name is chosen is worth the four lines.
SPIFFS_MAX_PATH_LEN = 31
SPIFFS_PREFIX = "/music/"


def write_track(stereo, name, title):
    """Encode one finished mix and report what it cost."""
    path_in_image = SPIFFS_PREFIX + name + ".mp3"
    if len(path_in_image) > SPIFFS_MAX_PATH_LEN:
        raise SystemExit(
            "Track name too long for SPIFFS: %s is %d characters, limit is %d.\n"
            "Shorten it to at most %d characters before the extension."
            % (path_in_image, len(path_in_image), SPIFFS_MAX_PATH_LEN,
               SPIFFS_MAX_PATH_LEN - len(SPIFFS_PREFIX) - 4))

    wav_path = os.path.join(OUTPUT, name + ".wav")
    mp3_path = os.path.join(OUTPUT, name + ".mp3")

    pcm = (stereo * 32000).astype("<i2")
    with wave.open(wav_path, "wb") as handle:
        handle.setnchannels(2)
        handle.setsampwidth(2)
        handle.setframerate(SAMPLE_RATE)
        handle.writeframes(pcm.tobytes())

    subprocess.run(
        ["ffmpeg", "-y", "-loglevel", "error", "-i", wav_path,
         "-codec:a", "libmp3lame", "-b:a", BITRATE,
         "-metadata", "title=" + title,
         "-metadata", "comment=Public domain composition; recording released under CC0",
         mp3_path],
        check=True)

    os.remove(wav_path)

    size = os.path.getsize(mp3_path)
    print("  %-28s %5.1f s  %6.1f KB" % (name + ".mp3",
                                         len(pcm) / SAMPLE_RATE, size / 1024.0))
    return size


def main():
    if not os.path.isdir(OUTPUT):
        os.makedirs(OUTPUT)

    print("Rendering public-domain music into spiffs/music/")

    total = 0

    # 108 BPM in 4/4. Brisk enough to be the tune people know, slow enough that
    # the reverb tail has somewhere to go.
    total += write_track(
        render_piece(ODE_MELODY, ODE_CHORDS, bpm=108, beats_per_bar=4,
                     repeats=2, melody_level=0.21),
        "ode-to-joy", "Ode to Joy (Beethoven, 1824)")

    # 3/8 at 132 eighth-notes per minute, which is the usual walking pace for
    # this one. Three repeats, because the section is short.
    total += write_track(
        render_piece(ELISE_MELODY, ELISE_CHORDS, bpm=132, beats_per_bar=3,
                     repeats=2, melody_level=0.20),
        "fur-elise", "Fur Elise (Beethoven, WoO 59)")

    # 120 BPM in 4/4 — a serenade, so it walks rather than marches.
    total += write_track(
        render_piece(NACHTMUSIK_MELODY, NACHTMUSIK_CHORDS, bpm=120, beats_per_bar=4,
                     repeats=3, melody_level=0.21),
        "nachtmusik", "Eine kleine Nachtmusik (Mozart, K. 525)")

    # Brisker again. Spring is meant to sound like something waking up.
    total += write_track(
        render_piece(SPRING_MELODY, SPRING_CHORDS, bpm=132, beats_per_bar=4,
                     repeats=4, melody_level=0.20),
        "vivaldi-spring", "Spring (Vivaldi, Op. 8 No. 1)")

    print("\n  %.1f KB of music total." % (total / 1024.0))
    print("  Compositions are public domain; these recordings are CC0.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
