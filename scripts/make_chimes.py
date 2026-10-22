#!/usr/bin/env python3
"""
make_chimes.py — Generate the desk buddy's notification sounds.

SPDX-License-Identifier: Unlicense OR CC0-1.0

The buddy's four chimes are short synthesised tones rather than recordings, so
they can be regenerated, tweaked and version-controlled as a few lines of maths
instead of binary blobs.

Run it from the project folder:

    python tools/make_chimes.py

It writes 16 kHz, 16-bit, stereo WAV files into spiffs/, which is the format the
ES8311 codec is already configured for, so no resampling happens at runtime.

Want a different personality? Edit the CHIMES table at the bottom. Each entry is
a list of (frequency in hertz, duration in seconds) notes.
"""

import math
import os
import struct
import wave

SAMPLE_RATE = 16000
CHANNELS = 2
SAMPLE_WIDTH_BYTES = 2

# Peak amplitude as a fraction of full scale. Kept well below 1.0 so the chimes
# sit under speech rather than startling anybody.
PEAK_AMPLITUDE = 0.28

# Length of the fade applied to the start and end of every note, in seconds.
# Without it each note begins and ends with an audible click.
FADE_SECONDS = 0.008


def render_note(frequency_hz, duration_seconds):
    """Render one sine tone with soft edges, as a list of floats in [-1, 1]."""
    total_samples = int(SAMPLE_RATE * duration_seconds)
    fade_samples = max(1, int(SAMPLE_RATE * FADE_SECONDS))
    samples = []

    for index in range(total_samples):
        # The tone itself, plus a quiet octave above to give it some sparkle.
        phase = 2.0 * math.pi * frequency_hz * index / SAMPLE_RATE
        value = math.sin(phase) + 0.22 * math.sin(2.0 * phase)
        value /= 1.22

        # Fade in at the start, fade out at the end.
        if index < fade_samples:
            value *= index / fade_samples
        elif index > total_samples - fade_samples:
            value *= (total_samples - index) / fade_samples

        samples.append(value * PEAK_AMPLITUDE)

    return samples


def render_chime(notes):
    """Render a sequence of (frequency, duration) notes back to back."""
    samples = []
    for frequency_hz, duration_seconds in notes:
        samples.extend(render_note(frequency_hz, duration_seconds))
    return samples


def write_wav(path, samples):
    """Write mono float samples out as a stereo 16-bit WAV file."""
    frames = bytearray()
    for value in samples:
        clamped = max(-1.0, min(1.0, value))
        as_integer = int(clamped * 32767)
        # The same sample in both channels: the codec is opened in stereo.
        frames += struct.pack("<hh", as_integer, as_integer)

    with wave.open(path, "wb") as wav_file:
        wav_file.setnchannels(CHANNELS)
        wav_file.setsampwidth(SAMPLE_WIDTH_BYTES)
        wav_file.setframerate(SAMPLE_RATE)
        wav_file.writeframes(bytes(frames))

    print("wrote {} ({:.2f} s)".format(path, len(samples) / SAMPLE_RATE))


# Frequencies, roughly, of the notes used below.
E5, G5, A5, B5, C6, E6 = 659.3, 784.0, 880.0, 987.8, 1046.5, 1318.5
A3, C4, E4 = 220.0, 261.6, 329.6

CHIMES = {
    # Rising two-note chirp: "I am listening".
    "chime_wake.wav": [(A5, 0.075), (E6, 0.115)],

    # Soft confirmation: "got it".
    "chime_understood.wav": [(C6, 0.065), (G5, 0.095)],

    # Quiet single blip while the buddy waits on the network.
    "chime_thinking.wav": [(B5, 0.055), (0.0, 0.045), (B5, 0.055)],

    # Falling low tone: "that did not work".
    "chime_error.wav": [(E4, 0.100), (C4, 0.100), (A3, 0.150)],
}


def main():
    project_root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    output_folder = os.path.join(project_root, "spiffs")
    os.makedirs(output_folder, exist_ok=True)

    for filename, notes in CHIMES.items():
        write_wav(os.path.join(output_folder, filename), render_chime(notes))

    print("\nDone. Rebuild and flash to hear them:  idf.py build flash monitor")


if __name__ == "__main__":
    main()
