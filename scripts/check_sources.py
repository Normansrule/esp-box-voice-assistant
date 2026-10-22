#!/usr/bin/env python3
"""
check_sources.py — A quick static sanity check of the firmware sources.

SPDX-License-Identifier: Unlicense OR CC0-1.0

This does NOT replace a compiler. What it does is catch the class of mistakes
that are easy to make while moving code between files and painful to find later:

  * a function declared in a header with no definition anywhere
  * a call to a project function that does not exist
  * an #include of a project header that is not on the include path
  * a .c file that exists but is missing from main/CMakeLists.txt
  * unbalanced braces or parentheses

Run it from the project folder:

    python tools/check_sources.py
"""

import os
import re
import sys

PROJECT_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MAIN_DIR = os.path.join(PROJECT_ROOT, "main")

# Longest path SPIFFS can store, in characters. The on-flash field is 32 bytes
# and one of them is the terminator. See the check near the end of main().
SPIFFS_MAX_PATH_LEN = 31

# Prefixes that identify a function as belonging to this firmware rather than to
# ESP-IDF, LVGL or the C library.
PROJECT_PREFIXES = (
    "buddy_", "face_", "hw_", "net_", "audio_", "voice_", "ui_",
)

# Names that start with a project prefix but belong to a managed component.
EXTERNAL_PREFIXES = (
    "audio_player_",    # chmorgan/esp-audio-player
)

# Headers that come from ESP-IDF or a managed component rather than main/.
EXTERNAL_HEADERS = {
    "lvgl.h",
    "nvs.h", "nvs_flash.h",
    "audio_player.h",
    "OpenAI.h",
    "bsp_board.h",
    "iot_button.h",
    "model_path.h",
}


def source_files():
    """Every .c and .h file under main/, as (absolute path, text) pairs."""
    for folder, _subfolders, filenames in os.walk(MAIN_DIR):
        for filename in sorted(filenames):
            if filename.endswith((".c", ".h")):
                path = os.path.join(folder, filename)
                with open(path, "r", encoding="utf-8") as handle:
                    yield path, handle.read()


def strip_comments_and_strings(text):
    """
    Remove comments and string literals so they cannot confuse the parsing.

    One pass, whichever starts first wins. Three separate passes got
    "http://" wrong: the // inside the string was taken for a comment and the
    rest of the line, closing brackets included, went with it.
    """
    token = re.compile(r'"(?:[^"\\\n]|\\.)*"'          # string literal
                       r"|'(?:[^'\\\n]|\\.)*'"          # character literal
                       r"|/\*.*?\*/"                     # block comment
                       r"|//[^\n]*", re.S)               # line comment

    def replace(match):
        found = match.group(0)
        if found.startswith('"'):
            return '""'
        if found.startswith("'"):
            return "' '"
        return " "

    return token.sub(replace, text)


def relative(path):
    return os.path.relpath(path, PROJECT_ROOT).replace("\\", "/")


# MPEG-1 Layer III frame header tables, indexed exactly as the bits are.
MP3_BITRATES_KBPS = [None, 32, 40, 48, 56, 64, 80, 96,
                     112, 128, 160, 192, 224, 256, 320, None]
MP3_SAMPLE_RATES  = [44100, 48000, 32000, None]


def check_playlist(music_dir):
    """
    Walk every frame of every .mp3 and report anything the box cannot play.

    Worth doing by hand rather than trusting the encoder. A track that plays
    perfectly on a computer can still be wrong for this firmware: a stray byte
    between frames, a sample rate the resampler was not built for, or a second
    rate appearing halfway through all sound like "that one song crashes it",
    and on a device with no screen for errors that is a miserable thing to
    diagnose. Ninety lines here turns it into one line of output.

    Pure standard library on purpose — this has to run in the same plain Python
    as the rest of the checks, with no ffmpeg and no pip install.
    """
    problems = []

    if not os.path.isdir(music_dir):
        return problems

    for filename in sorted(os.listdir(music_dir)):
        if not filename.lower().endswith(".mp3"):
            continue

        path = os.path.join(music_dir, filename)
        with open(path, "rb") as handle:
            data = handle.read()

        offset = 0

        # Skip an ID3v2 tag. Its length is four seven-bit bytes, because the
        # high bit is reserved to keep a tag from ever looking like a frame.
        if data[:3] == b"ID3" and len(data) > 10:
            size = ((data[6] & 0x7F) << 21) | ((data[7] & 0x7F) << 14) | \
                   ((data[8] & 0x7F) << 7) | (data[9] & 0x7F)
            offset = 10 + size

        frames = 0
        stray_bytes = 0
        rates = set()

        while offset < len(data) - 4:
            # A frame starts with eleven set bits.
            if data[offset] != 0xFF or (data[offset + 1] & 0xE0) != 0xE0:
                stray_bytes += 1
                offset += 1
                continue

            version = (data[offset + 1] >> 3) & 0x03    # 3 == MPEG-1
            layer   = (data[offset + 1] >> 1) & 0x03    # 1 == Layer III
            bitrate = MP3_BITRATES_KBPS[(data[offset + 2] >> 4) & 0x0F]
            rate    = MP3_SAMPLE_RATES[(data[offset + 2] >> 2) & 0x03]
            padding = (data[offset + 2] >> 1) & 0x01

            if version != 3 or layer != 1 or bitrate is None or rate is None:
                stray_bytes += 1
                offset += 1
                continue

            rates.add(rate)
            frames += 1
            offset += (144 * bitrate * 1000) // rate + padding

        name = "spiffs/music/" + filename

        if frames == 0:
            problems.append("%s holds no playable MPEG-1 Layer III frames" % name)
            continue

        # A handful of trailing bytes is normal padding; a run of them is not.
        if stray_bytes > 512:
            problems.append(
                "%s has %d bytes that are not part of any frame; the decoder "
                "will have to resynchronise mid-track"
                % (name, stray_bytes))

        if len(rates) > 1:
            problems.append(
                "%s changes sample rate mid-track (%s); the resampler is set up "
                "once per track and cannot follow it"
                % (name, ", ".join("%d Hz" % r for r in sorted(rates))))

        seconds = frames * 1152.0 / float(max(rates))
        if seconds < 1.0:
            problems.append("%s is only %.2f s long — is it truncated?" % (name, seconds))

    return problems


def main():
    problems = []

    files = list(source_files())
    if not files:
        print("No sources found under main/ — is this the right folder?")
        return 1

    header_names = {os.path.basename(p) for p, _ in files if p.endswith(".h")}

    declared = {}     # function name -> header it was declared in
    defined = {}      # function name -> file it was defined in
    called = {}       # function name -> set of files that call it

    # Matches a function definition: "type name(args)\n{" at column zero.
    definition_pattern = re.compile(
        r"^[A-Za-z_][A-Za-z0-9_ \*]*?\b([a-z_][a-z0-9_]*)\s*\([^;)]*\)\s*$", re.M)
    # Matches a prototype ending in a semicolon.
    prototype_pattern = re.compile(
        r"\b([a-z_][a-z0-9_]*)\s*\([^;{)]*\)\s*;", re.M)
    call_pattern = re.compile(r"\b([a-z_][a-z0-9_]*)\s*\(")

    for path, raw in files:
        text = strip_comments_and_strings(raw)
        name = relative(path)

        # --- balance -------------------------------------------------------
        for opener, closer, label in (("{", "}", "braces"), ("(", ")", "parens")):
            if text.count(opener) != text.count(closer):
                problems.append(
                    "%s: unbalanced %s (%d vs %d)"
                    % (name, label, text.count(opener), text.count(closer)))

        # --- includes ------------------------------------------------------
        for included in re.findall(r'#include\s+"([^"]+)"', raw):
            if included.startswith(("bsp/", "freertos/", "driver/", "esp_")):
                continue
            if included in EXTERNAL_HEADERS:
                continue
            if os.path.basename(included) not in header_names:
                problems.append("%s: includes \"%s\", which is not a project "
                                "header or a known component header"
                                % (name, included))

        # --- declarations, definitions and calls ---------------------------
        if path.endswith(".h"):
            for match in prototype_pattern.finditer(text):
                function = match.group(1)
                if function.startswith(PROJECT_PREFIXES):
                    declared[function] = name
        else:
            for match in definition_pattern.finditer(text):
                function = match.group(1)
                start = match.end()
                remainder = text[start:start + 4].lstrip()
                if remainder.startswith("{"):
                    defined[function] = name

        for match in call_pattern.finditer(text):
            function = match.group(1)
            if (function.startswith(PROJECT_PREFIXES)
                    and not function.startswith(EXTERNAL_PREFIXES)):
                called.setdefault(function, set()).add(name)

    # --- declared but never defined -----------------------------------------
    for function, header in sorted(declared.items()):
        if function not in defined:
            problems.append("%s: declares %s() but nothing defines it"
                            % (header, function))

    # --- called but neither declared nor defined ----------------------------
    for function, callers in sorted(called.items()):
        if function not in defined and function not in declared:
            problems.append("%s() is called from %s but does not exist"
                            % (function, ", ".join(sorted(callers))))

    # --- every .c is in the build -------------------------------------------
    cmake_path = os.path.join(MAIN_DIR, "CMakeLists.txt")
    with open(cmake_path, "r", encoding="utf-8") as handle:
        cmake_text = handle.read()

    for path, _text in files:
        if not path.endswith(".c"):
            continue
        rel_to_main = os.path.relpath(path, MAIN_DIR).replace("\\", "/")
        if '"%s"' % rel_to_main not in cmake_text:
            problems.append("main/CMakeLists.txt does not build %s" % rel_to_main)

    # --- names the filesystem image can actually hold ------------------------
    #
    # SPIFFS stores an object name in a fixed field, 32 bytes including the
    # terminator, so a path may be 31 characters. Going over does not fail
    # here, in a file you just added: it fails minutes later inside
    # spiffsgen.py while the firmware image is being built, with an error
    # about an "object name" and no hint that the cause was a file name
    # chosen by hand. Checking it here costs nothing and puts the message
    # where somebody can act on it.
    spiffs_dir = os.path.join(PROJECT_ROOT, "spiffs")

    if os.path.isdir(spiffs_dir):
        for folder, _subfolders, filenames in os.walk(spiffs_dir):
            for filename in filenames:
                full = os.path.join(folder, filename)
                in_image = "/" + os.path.relpath(full, spiffs_dir).replace("\\", "/")

                if len(in_image) > SPIFFS_MAX_PATH_LEN:
                    problems.append(
                        "%s is %d characters; SPIFFS allows %d, so the image "
                        "will not build" % (in_image, len(in_image), SPIFFS_MAX_PATH_LEN))

    # --- music the decoder can actually play ---------------------------------
    problems.extend(check_playlist(os.path.join(spiffs_dir, "music")))

    # --- report --------------------------------------------------------------
    source_count = sum(1 for p, _ in files if p.endswith(".c"))
    header_count = len(files) - source_count
    print("Checked %d source files and %d headers." % (source_count, header_count))
    print("Found %d project functions defined, %d declared."
          % (len(defined), len(declared)))
    print()

    if not problems:
        print("No problems found.")
        return 0

    print("%d problem(s):" % len(problems))
    for problem in problems:
        print("  - " + problem)
    return 1


if __name__ == "__main__":
    sys.exit(main())
