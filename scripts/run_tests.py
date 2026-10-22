#!/usr/bin/env python3
"""
run_tests.py — Build and run the host tests.

SPDX-License-Identifier: Unlicense OR CC0-1.0

These tests run on your computer, not on the box. They compile the parts of the
firmware that are pure logic — the expression table, the antic scripts, the
voice vocabulary, the mood-tag parser — with an ordinary C compiler and check
them over. No hardware, no ESP-IDF, no network.

Run it from the project folder:

    python scripts\\run_tests.py

It also runs the static source check first, since a missing definition is
quicker to read about there than as a linker error here.

Which compiler it uses: gcc, cc or clang from the PATH, then the usual Windows
install locations for MSYS2 and LLVM. Every candidate is checked by building and
RUNNING a two-line program first.

That check matters. In an ESP-IDF terminal, plain `clang` on the PATH is
Espressif's esp-clang, which builds for the chip — it compiles this suite
happily and then dies at the link step with "ld command failed due to signal",
which reads like a broken test suite rather than the wrong compiler.

If no desktop compiler is available the script says so, explains that the
firmware is fine, and exits without pretending the tests ran.
"""

import os
import shutil
import subprocess
import sys
import tempfile

PROJECT_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TESTS_DIR = os.path.join(PROJECT_ROOT, "tests")
MAIN_DIR = os.path.join(PROJECT_ROOT, "main")
BUILD_DIR = os.path.join(TESTS_DIR, "build")

# The firmware sources under test. Every one of these must be free of hardware
# dependencies — that is the point of keeping them separate in the first place.
SOURCES_UNDER_TEST = [
    os.path.join(MAIN_DIR, "face", "face_pose.c"),
    os.path.join(MAIN_DIR, "face", "face_emotions.c"),
    os.path.join(MAIN_DIR, "voice", "voice_commands.c"),
    os.path.join(MAIN_DIR, "voice", "voice_mood_tag.c"),
    os.path.join(MAIN_DIR, "voice", "voice_math.c"),
    os.path.join(MAIN_DIR, "audio", "audio_resampler.c"),
    os.path.join(MAIN_DIR, "audio", "audio_voice_hash.c"),
    os.path.join(MAIN_DIR, "core", "buddy_timer.c"),
    os.path.join(MAIN_DIR, "core", "buddy_api_key.c"),
    os.path.join(MAIN_DIR, "core", "buddy_calculator.c"),
    os.path.join(MAIN_DIR, "voice", "voice_number_words.c"),
    os.path.join(MAIN_DIR, "voice", "voice_speech_parts.c"),
    os.path.join(MAIN_DIR, "voice", "voice_speech_gate.c"),
    os.path.join(MAIN_DIR, "voice", "voice_router.c"),
]

TEST_SOURCE = os.path.join(TESTS_DIR, "test_desk_buddy.c")

INCLUDE_DIRS = [
    os.path.join(TESTS_DIR, "stubs"),      # must come first: shadows esp_err.h
    MAIN_DIR,
    os.path.join(MAIN_DIR, "core"),
    os.path.join(MAIN_DIR, "face"),
    os.path.join(MAIN_DIR, "voice"),
    os.path.join(MAIN_DIR, "audio"),
]

WARNING_FLAGS = [
    "-std=c11",
    "-Wall",
    "-Wextra",
    "-Werror",
    "-Wno-unused-parameter",
    "-Wno-missing-field-initializers",   # designated initialisers by design
    "-g",
]


# Compilers to try, in order. Names first (found via PATH), then the places a
# desktop compiler usually installs itself on Windows, so having MSYS2 or LLVM
# on the machine is enough even if neither is on the PATH.
COMPILER_CANDIDATES = [
    "gcc",
    "cc",
    "clang",
    r"C:\msys64\mingw64\bin\gcc.exe",
    r"C:\msys64\ucrt64\bin\gcc.exe",
    r"C:\MinGW\bin\gcc.exe",
    r"C:\Program Files\LLVM\bin\clang.exe",
]


def compiles_and_runs_here(compiler):
    """
    Compile a two-line program and run it.

    This is the only honest test of "can this compiler build something this
    machine can execute". Checking the name is not enough: inside an ESP-IDF
    terminal, plain `clang` on the PATH is Espressif's esp-clang, which builds
    for the chip. It compiles the test suite perfectly happily and then fails at
    the link step with

        clang: error: ld command failed due to signal

    which looks like a broken test suite rather than the wrong compiler.

    The probe returns 42 rather than 0 so that a binary which cannot launch at
    all is not mistaken for one that ran and succeeded.
    """
    probe_dir = tempfile.mkdtemp(prefix="buddy-compiler-probe-")
    try:
        source = os.path.join(probe_dir, "probe.c")
        with open(source, "w", encoding="utf-8") as handle:
            handle.write("int main(void) { return 42; }\n")

        binary = os.path.join(probe_dir, "probe.exe" if os.name == "nt" else "probe")

        built = subprocess.run([compiler, source, "-o", binary],
                               stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        if built.returncode != 0 or not os.path.exists(binary):
            return False

        ran = subprocess.run([binary],
                             stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        return ran.returncode == 42
    except (OSError, subprocess.SubprocessError):
        return False
    finally:
        shutil.rmtree(probe_dir, ignore_errors=True)


def find_compiler():
    """Return a compiler that can build and run something here, or None."""
    rejected = []

    for candidate in COMPILER_CANDIDATES:
        found = shutil.which(candidate) if os.sep not in candidate else candidate
        if not found or not os.path.exists(found):
            continue

        if compiles_and_runs_here(found):
            return found, rejected

        rejected.append(found)

    return None, rejected


def main():
    print()
    print("Desk Buddy test run")
    print("-------------------")

    # --- 1. The static check --------------------------------------------
    checker = os.path.join(PROJECT_ROOT, "scripts", "check_sources.py")
    print("\n[1/3] Static source check")
    result = subprocess.run([sys.executable, checker])
    if result.returncode != 0:
        print("\nStatic check failed — fix that before running the tests.")
        return 1

    # --- 2. Compile -------------------------------------------------------
    compiler, rejected = find_compiler()
    if compiler is None:
        print("\n[2/3] No desktop C compiler available — skipping the host tests.")
        print()
        if rejected:
            print("  These were found but build for something other than this machine:")
            for path in rejected:
                print("      %s" % path)
            print()
            print("  That is normal in an ESP-IDF terminal: the compilers on the PATH")
            print("  there target the chip, not your PC.")
            print()
        print("  NOTHING IS WRONG WITH THE FIRMWARE. The static check above passed,")
        print("  and these tests are a development aid, not a gate. Carry on:")
        print()
        print("      idf.py build")
        print()
        print("  The buddy also checks itself at every boot, and Settings has a")
        print("  Diagnostics row that shows the result on screen.")
        print()
        print("  To run them here anyway, install a desktop compiler:")
        print("      winget install -e --id MSYS2.MSYS2")
        print("  then, in the MSYS2 terminal:")
        print("      pacman -S mingw-w64-x86_64-gcc")
        print("  This script finds it afterwards without any PATH changes.")
        return 2

    print("\n[2/3] Compiling with %s" % compiler)

    os.makedirs(BUILD_DIR, exist_ok=True)
    executable = os.path.join(BUILD_DIR, "desk_buddy_tests")
    if os.name == "nt":
        executable += ".exe"

    command = [compiler] + WARNING_FLAGS
    for directory in INCLUDE_DIRS:
        command += ["-I", directory]
    command += [TEST_SOURCE] + SOURCES_UNDER_TEST
    command += ["-o", executable]
    # audio_resampler.c works out its filter coefficient with expf(), which on
    # Linux lives in a separate library. Harmless everywhere else.
    command += ["-lm"]

    result = subprocess.run(command)
    if result.returncode != 0:
        print("\nCompilation failed. The output above says why.")
        return 1

    # --- 3. Run -----------------------------------------------------------
    print("\n[3/3] Running")
    result = subprocess.run([executable])

    if result.returncode == 0:
        print("Everything passed. Safe to build and flash.")
    else:
        print("Tests failed — see the FAIL lines above.")
    return result.returncode


if __name__ == "__main__":
    sys.exit(main())
