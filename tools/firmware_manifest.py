#!/usr/bin/env python3
"""
firmware_manifest.py - describe a firmware release for the flasher page.

SPDX-License-Identifier: Unlicense OR CC0-1.0

Reads a folder holding the images of one build plus its flasher_args.json,
which is how every release is published (see .github/workflows/release.yml),
and writes manifest.json beside them: what goes where, how big it is, and its
SHA-256 so web/connect/ can check every download before writing it.

    python tools/firmware_manifest.py DIR --version 3.7.1 [--commit SHA]
    python tools/firmware_manifest.py --from-build build DIR --version 3.7.1

--from-build first copies the images out of an ESP-IDF build folder, flat.

Refuses, and writes nothing, if any image contains something shaped like an
OpenAI key. storage.bin is a copy of spiffs/, so a build made with a key in
spiffs/openai_key.txt carries that key; such an image must never be published.
"""

import argparse
import datetime
import hashlib
import json
import re
import shutil
import sys
from pathlib import Path

ROLES = ("bootloader", "partition-table", "otadata", "app", "storage", "model")
KEY_PATTERN = re.compile(rb"sk-(proj-)?[A-Za-z0-9_-]{20,}")


def copy_from_build(build, out):
    args = json.loads((build / "flasher_args.json").read_text())
    out.mkdir(parents=True, exist_ok=True)
    shutil.copy2(build / "flasher_args.json", out / "flasher_args.json")
    for role in ROLES:
        if role in args:
            shutil.copy2(build / args[role]["file"], out / Path(args[role]["file"]).name)


def build_manifest(folder, version, commit=None):
    args = json.loads((folder / "flasher_args.json").read_text())
    parts = []
    for role in ROLES:
        entry = args.get(role)
        if not entry:
            continue
        name = Path(entry["file"]).name
        data = (folder / name).read_bytes()
        if KEY_PATTERN.search(data):
            raise SystemExit("REFUSING: %s contains what looks like an API key. "
                             "Rebuild without spiffs/openai_key.txt." % name)
        parts.append({
            "name": name,
            "file": name,
            "role": role,
            "offset": int(entry["offset"], 16),
            "size": len(data),
            "sha256": hashlib.sha256(data).hexdigest(),
        })
    if not any(p["role"] == "app" for p in parts):
        raise SystemExit("flasher_args.json in %s names no app image" % folder)
    manifest = {
        "name": "ESP-BOX Desk Buddy",
        "version": version.lstrip("v"),
        "chip": args.get("extra_esptool_args", {}).get("chip", "esp32s3"),
        "flash": args.get("flash_settings", {}),
        "date": datetime.date.today().isoformat(),
        "parts": sorted(parts, key=lambda p: p["offset"]),
    }
    if commit:
        manifest["commit"] = commit
    return manifest


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("folder", type=Path)
    parser.add_argument("--version", required=True)
    parser.add_argument("--commit")
    parser.add_argument("--from-build", type=Path)
    options = parser.parse_args()

    if options.from_build:
        copy_from_build(options.from_build, options.folder)
    manifest = build_manifest(options.folder, options.version, options.commit)
    (options.folder / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    total = sum(p["size"] for p in manifest["parts"])
    print("manifest -> %s (%d images, %.1f MB)" % (options.folder / "manifest.json",
                                                    len(manifest["parts"]), total / 1048576))


if __name__ == "__main__":
    sys.exit(main())
