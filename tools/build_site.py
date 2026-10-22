#!/usr/bin/env python3
"""
build_site.py - assemble the GitHub Pages site into _site/.

SPDX-License-Identifier: Unlicense OR CC0-1.0

Copies web/ as-is, then adds the firmware's own audio so the simulator plays
exactly what the device plays:

    spiffs/voice/*.mp3    -> _site/voice/   (offline reply clips)
    spiffs/chime_*.wav    -> _site/audio/   (wake / understood / ... chimes)

Regenerates web/assets/firmware-data.js first so the site can never ship
tables that disagree with the firmware source.

    python tools/build_site.py
    python -m http.server 8000 --directory _site
"""

import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SITE = ROOT / "_site"


def main():
    subprocess.run([sys.executable, str(ROOT / "tools" / "extract_firmware_data.py")], check=True)
    if SITE.exists():
        shutil.rmtree(SITE)
    shutil.copytree(ROOT / "web", SITE)
    (SITE / "voice").mkdir()
    (SITE / "audio").mkdir()
    clips = sorted((ROOT / "spiffs" / "voice").glob("*.mp3"))
    for clip in clips:
        shutil.copy2(clip, SITE / "voice" / clip.name)
    chimes = sorted((ROOT / "spiffs").glob("chime_*.wav"))
    for chime in chimes:
        shutil.copy2(chime, SITE / "audio" / chime.name)
    (SITE / ".nojekyll").write_text("")
    print("site -> %s (%d voice clips, %d chimes)" % (SITE.relative_to(ROOT), len(clips), len(chimes)))


if __name__ == "__main__":
    main()
