"""
make_voice_clips.py — renders the buddy's offline voice.

WHAT THIS IS FOR
----------------
Without a network the buddy has always been mute: it shows its answer in the
speech bubble and that is the whole reply. Speaking out loud went through the
online text-to-speech service, so no Wi-Fi and no API key meant no voice.

This script fixes that for every answer the buddy already knows by heart. It
reads the fixed reply strings straight out of voice_local_brain.c, speaks each
one with eSpeak NG, and writes the result into spiffs/voice/ as a small MP3
named after a hash of the text. At runtime audio_voice.c hashes the reply it is
about to show and looks for a file of that name — a hit is played through the
speaker, a miss falls back to the bubble alone, exactly as before.

Naming clips by content hash rather than by index is what keeps the two sides
honest. Reword a reply and its hash changes, the lookup misses, and the buddy
goes quiet for that one line rather than confidently saying something else.

WHAT IT DELIBERATELY DOES NOT COVER
-----------------------------------
Replies assembled at runtime — the time, the temperature, a track name, the
answer to a sum — have no fixed text to hash, so they get no clip. They are
still spoken when the online voice is configured. Covering them offline would
mean stitching together word-by-word clips, which sounds considerably worse
than either option and costs several times the flash.

USAGE
-----
    sudo apt-get install espeak-ng ffmpeg
    python3 scripts/make_voice_clips.py

Then rebuild: the files are baked into the storage partition at build time.
"""

import os
import re
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
PROJECT = os.path.dirname(HERE)
# Every file that holds finished reply text. voice_local_brain.c is almost all
# of it; voice_assistant.c owns the handful of lines the buddy says on its own
# initiative rather than in answer to something — the timer going off.
SOURCES = [
    os.path.join(PROJECT, "main", "voice", "voice_local_brain.c"),
    os.path.join(PROJECT, "main", "voice", "voice_assistant.c"),
]

# Files whose EVERY string literal is a recording, with no sentence-shape filter.
#
# The fixed replies above are picked out by looking for something that reads
# like a finished sentence, because those files are full of format strings and
# log messages that must not be recorded. These two are different: they exist
# solely to hold the pieces an answer with a number in it is assembled from, so
# "in the afternoon" and "forty seven" belong even though neither is a sentence.
# See main/voice/voice_speech_parts.h for how the pieces go back together.
LITERAL_SOURCES = [
    os.path.join(PROJECT, "main", "voice", "voice_number_words.c"),
    os.path.join(PROJECT, "main", "voice", "voice_speech_parts.c"),
]
OUTPUT = os.path.join(PROJECT, "spiffs", "voice")

# The codec is pinned to 16 kHz — see main/audio/audio_resampler.h for why —
# so rendering at 16 kHz means playback needs no conversion at all.
SAMPLE_RATE = 16000
# 24 kbps mono-source speech at 16 kHz. Dropping from 32k costs nothing
# audible on a speaker this size and saves about a quarter of the flash the
# voice takes up — which matters more than it sounds, because the storage
# partition also holds the music.
BITRATE = "24k"

# eSpeak settings. 175 words per minute is brisk but unhurried; the slightly
# lowered pitch stops it sounding shrill through a small speaker.
VOICE = "en-us+m3"
SPEED = "175"
PITCH = "42"


def fnv1a(text):
    """
    32-bit FNV-1a over the raw bytes of the reply.

    audio_voice.c computes this exact function. If you change one, change the
    other, and re-run the host tests — they check a handful of known strings
    against hardcoded values precisely so the two cannot drift apart silently.
    """
    value = 0x811C9DC5
    for byte in text.encode("utf-8"):
        value ^= byte
        value = (value * 0x01000193) & 0xFFFFFFFF
    return value


def unescape(literal):
    """Turn the body of a C string literal into the string the firmware sees."""
    return (literal
            .replace('\\"', '"')
            .replace("\\n", "\n")
            .replace("\\t", "\t")
            .replace("\\\\", "\\"))


def extract_fixed_replies(paths):
    """
    Pull every fixed reply string out of the C source.

    Adjacent literals are joined, because C concatenates them into ONE runtime
    string — miss that and every multi-line reply hashes wrongly and is silent.
    Anything holding a printf specifier is skipped: those are templates, filled
    in at runtime, and never appear verbatim.
    """
    literal = r'"((?:[^"\\]|\\.)*)"'
    replies = []
    seen = set()

    runs = []
    for path in paths:
        source = open(path, encoding="utf-8").read()

        # Comments first, or the prose inside them ends up in the vocabulary.
        source = re.sub(r"/\*.*?\*/", "", source, flags=re.S)
        source = re.sub(r"//[^\n]*", "", source)

        runs.extend(re.finditer(r"%s(?:\s*%s)*" % (literal, literal), source))

    for run in runs:
        parts = re.findall(literal, run.group(0))
        text = unescape("".join(parts))

        if "%" in text:
            continue                       # a template, not a finished reply

        # A reply is a SENTENCE: it opens with a capital and closes with
        # punctuation. Testing that rather than a minimum length is what lets
        # "Heads." through while still excluding log tags, file paths and
        # format fragments — an earlier length test silently dropped every
        # one-word answer the coin flip could give.
        if not re.match(r"^[A-Z][^\n]*[.!?]$", text):
            continue
        if not re.search(r"[a-zA-Z]{3}", text):
            continue
        if text in seen:
            continue

        seen.add(text)
        replies.append(text)

    return replies


def extract_every_literal(paths):
    """
    Pull EVERY string literal out of the given files.

    No sentence-shape filter, because these files hold sentence FRAGMENTS: the
    number words and the connector phrases an assembled answer is built from.
    "in the afternoon" and "forty seven" both need recordings and neither would
    survive the filter above.

    The header guards and include names are excluded by the same comment
    stripping plus a check that a literal contains letters and no punctuation
    that only appears in code.
    """
    literal = r'"((?:[^"\\]|\\.)*)"'
    fragments = []
    seen = set()

    for path in paths:
        source = open(path, encoding="utf-8").read()
        source = re.sub(r"/\*.*?\*/", "", source, flags=re.S)
        source = re.sub(r"//[^\n]*", "", source)

        # Drop the #include lines, whose literals are file names.
        source = re.sub(r"#\s*include[^\n]*", "", source)

        for match in re.finditer(literal, source):
            text = unescape(match.group(1))

            if not text or "%" in text or text in seen:
                continue
            if not re.search(r"[a-zA-Z]", text):
                continue

            seen.add(text)
            fragments.append(text)

    return fragments


def require(binary, package):
    if shutil.which(binary) is None:
        print("Need %s. Install it with:\n    sudo apt-get install %s"
              % (binary, package))
        sys.exit(1)


def render(text, destination):
    """Speak one line and encode it, returning the finished file size."""
    raw = destination + ".raw.wav"

    subprocess.run(
        ["espeak-ng", "-v", VOICE, "-s", SPEED, "-p", PITCH, "-w", raw, text],
        check=True, capture_output=True)

    # Stereo on purpose, even though eSpeak gives us mono. The music that is
    # known to play correctly on this board is stereo, and matching it means the
    # clips travel the exact same codec path rather than a mono one nothing else
    # exercises. The extra bytes are nearly free — joint stereo of a mono source
    # costs about a tenth.
    # -map_metadata -1 and -write_xing 0 matter more than they look.
    #
    # Clips are CONCATENATED to build an answer with a number in it — see
    # main/voice/voice_speech_parts.h — and the decoder reads the join as one
    # continuous stream. An ID3 tag or a Xing header sitting in the middle of
    # that stream is not audio, and the decoder either skips a frame or gives
    # up. Stripping both leaves nothing but MP3 frames, which concatenate
    # cleanly by design.
    subprocess.run(
        ["ffmpeg", "-y", "-loglevel", "error", "-i", raw,
         "-ar", str(SAMPLE_RATE), "-ac", "2",
         "-af", "silenceremove=start_periods=1:start_threshold=-45dB,"
                "areverse,silenceremove=start_periods=1:start_threshold=-45dB,areverse,"
                "loudnorm=I=-16:TP=-1.5:LRA=11",
         "-codec:a", "libmp3lame", "-b:a", BITRATE,
         "-map_metadata", "-1", "-write_xing", "0", "-id3v2_version", "0",
         destination],
        check=True, capture_output=True)

    os.remove(raw)
    return os.path.getsize(destination)


def main():
    require("espeak-ng", "espeak-ng")
    require("ffmpeg", "ffmpeg")

    replies = extract_fixed_replies(SOURCES)
    if not replies:
        print("Found no fixed replies — have the source files moved?")
        return 1

    fragments = extract_every_literal(LITERAL_SOURCES)
    if not fragments:
        print("Found no speech fragments — have the source files moved?")
        return 1

    print("%d whole replies, %d fragments to assemble the rest from"
          % (len(replies), len(fragments)))

    # Fragments after replies, and de-duplicated against them: a word can
    # legitimately be both, and rendering it twice would just waste flash.
    for fragment in fragments:
        if fragment not in replies:
            replies.append(fragment)

    if os.path.isdir(OUTPUT):
        shutil.rmtree(OUTPUT)
    os.makedirs(OUTPUT)

    total = 0
    manifest = []

    for text in replies:
        name = "%08x.mp3" % fnv1a(text)
        size = render(text, os.path.join(OUTPUT, name))
        total += size
        manifest.append((name, size, text))

    with open(os.path.join(OUTPUT, "README.txt"), "w", encoding="utf-8") as handle:
        handle.write(
            "The buddy's offline voice.\n"
            "==========================\n\n"
            "Generated by scripts/make_voice_clips.py — do not edit by hand.\n"
            "Each file is one spoken reply, named after a hash of its text, so\n"
            "audio_voice.c can find it without either side keeping a list.\n"
            "Re-run the script after changing any reply in voice_local_brain.c.\n\n")
        for name, size, text in manifest:
            handle.write("%s  %6d B  %s\n" % (name, size, text))

    print("%d clips, %.0f KB total, in spiffs/voice/" % (len(manifest), total / 1024.0))
    print("Largest:")
    for name, size, text in sorted(manifest, key=lambda row: -row[1])[:3]:
        print("  %6d B  %s" % (size, text[:64]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
