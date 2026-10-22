#!/usr/bin/env python3
"""
extract_firmware_data.py - pull the face and voice tables out of the firmware
source so the web simulator and the README renders draw exactly what the
device draws.

SPDX-License-Identifier: Unlicense OR CC0-1.0

Nothing here is hand-copied. The script reads:

    main/buddy_config.h          version, screen and face geometry
    main/face/face_theme.h       the colour palette
    main/face/face_emotions.c    every pose row, the names, the idle antics
    main/voice/voice_commands.c  the English offline phrase table

and writes web/assets/firmware-data.js (loaded by the simulator) plus a JSON
copy for anything else that wants it. Re-run it after editing any of those
files; CI fails if the committed output is stale (--check).

Usage, from the repository root or anywhere inside it:

    python tools/extract_firmware_data.py          # regenerate
    python tools/extract_firmware_data.py --check  # exit 1 if out of date
"""

import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
MAIN = ROOT / "main"
OUT_JS = ROOT / "web" / "assets" / "firmware-data.js"
OUT_JSON = ROOT / "web" / "assets" / "firmware-data.json"


def c_div(a, b):
    """C integer division truncates toward zero; Python's // floors."""
    q = abs(a) // abs(b)
    return q if (a >= 0) == (b >= 0) else -q


def read(path):
    return path.read_text(encoding="utf-8")


def parse_defines(text):
    out = {}
    for m in re.finditer(r"^#define\s+(\w+)\s+(.+?)\s*(?:/\*.*)?$", text, re.M):
        out[m.group(1)] = m.group(2).strip()
    return out


def evaluate(expr, scale, symbols):
    """Evaluate a small C integer expression using C truncating division."""
    expr = expr.strip()
    expr = expr.replace("true", "True").replace("false", "False")
    for name, value in symbols.items():
        expr = re.sub(r"\b%s\b" % name, str(value), expr)
    expr = re.sub(r"\b(?:S|FACE_SCALED)\(", "_scaled(", expr)
    if re.search(r"[^\w\s()+\-*/,]", expr):
        raise ValueError("unexpected expression: " + expr)
    return eval(expr, {"__builtins__": {}},
                {"_scaled": lambda px: c_div(px * scale, 100),
                 "True": True, "False": False})


def config_and_geometry():
    defs = parse_defines(read(MAIN / "buddy_config.h"))
    scale = int(defs["FACE_SCALE_PERCENT"])
    wanted = [
        "BUDDY_SCREEN_WIDTH", "BUDDY_SCREEN_HEIGHT",
        "FACE_EYE_SPACING_FROM_CENTRE", "FACE_EYE_CENTRE_Y", "FACE_EYE_HEIGHT",
        "FACE_EYE_LOOK_RANGE_X", "FACE_EYE_LOOK_RANGE_Y",
        "FACE_BROW_LENGTH", "FACE_BROW_THICKNESS", "FACE_BROW_GAP_ABOVE_EYE",
        "FACE_BROW_MIN_BASELINE_Y", "FACE_MOUTH_CENTRE_Y", "FACE_MOUTH_THICKNESS",
        "FACE_MOUTH_POINT_COUNT", "FACE_MOUTH_MAX_OPEN_HEIGHT",
        "FACE_BLUSH_DIAMETER", "FACE_BLUSH_OFFSET_X", "FACE_BLUSH_OFFSET_Y",
        "FACE_EXPRESSION_TRANSITION_MS", "FACE_BLINK_CLOSE_MS", "FACE_BLINK_OPEN_MS",
        "FACE_BLINK_INTERVAL_MIN_MS", "FACE_BLINK_INTERVAL_MAX_MS",
        "FACE_IDLE_ANTIC_INTERVAL_MIN_MS", "FACE_IDLE_ANTIC_INTERVAL_MAX_MS",
        "FACE_LIP_SYNC_REFRESH_MS", "BUDDY_LISTEN_WINDOW_MS",
        "BUDDY_DEFAULT_REQUESTS_PER_HOUR", "BUDDY_SENSOR_REFRESH_MS",
    ]
    geometry = {}
    for name in wanted:
        geometry[name] = evaluate(defs[name], scale, {})
    return {
        "name": defs["BUDDY_FIRMWARE_NAME"].strip('"'),
        "version": defs["BUDDY_FIRMWARE_VERSION"].strip('"'),
        "wake_word": defs["BUDDY_WAKE_WORD_ENGLISH"].strip('"'),
        "scale_percent": scale,
    }, geometry, scale


def view_constants():
    defs = parse_defines(read(MAIN / "face" / "face_view.c"))
    names = ["EYE_SHUT_HEIGHT", "EYE_SHUT_CRESCENT", "BROW_BASELINE_Y", "BROW_ARCH",
             "MOUTH_CANVAS_WIDTH", "MOUTH_BASELINE_Y"]
    return {n: int(defs[n]) for n in names}


def theme():
    defs = parse_defines(read(MAIN / "face" / "face_theme.h"))
    return {name[len("FACE_HEX_"):].lower(): "#" + value[2:].upper()
            for name, value in defs.items() if name.startswith("FACE_HEX_")}


def emotions(scale):
    src = read(MAIN / "face" / "face_emotions.c")
    symbols = {k: int(v) for k, v in
               re.findall(r"^#define\s+(EYE_W|EYE_H|EYE_R|MOUTH_W)\s+(\d+)", src, re.M)}

    names = dict(re.findall(r'\[FACE_EMOTION_(\w+)\]\s*=\s*"(\w+)"', src))
    table = src.split("s_emotion_poses[FACE_EMOTION_COUNT] = {", 1)[1]
    table = table.split("\n};", 1)[0]

    poses = []
    for m in re.finditer(r"\[FACE_EMOTION_(\w+)\]\s*=\s*\{(.*?)\n\s*\},", table, re.S):
        body = re.sub(r"/\*.*?\*/", "", m.group(2), flags=re.S)
        fields = {}
        for fm in re.finditer(r"\.(\w+)\s*=\s*([^,]+?)\s*,", body + ","):
            fields[fm.group(1)] = evaluate(fm.group(2), scale, symbols)
        poses.append({"id": m.group(1).lower(), "name": names[m.group(1)], "pose": fields})

    antics = []
    steps_by_array = {}
    for m in re.finditer(r"static const face_antic_step_t (\w+)\[\] = \{(.*?)\n\};", src, re.S):
        steps = []
        for row in re.finditer(r"\{\s*FACE_EMOTION_(\w+),([^}]*)\}", m.group(2)):
            nums = [int(x) for x in row.group(2).split(",") if x.strip()]
            steps.append({"emotion": names[row.group(1)], "look_x": nums[0], "look_y": nums[1],
                          "face_offset_x": nums[2], "face_offset_y": nums[3],
                          "transition_ms": nums[4], "hold_ms": nums[5]})
        steps_by_array[m.group(1)] = steps
    for m in re.finditer(r'ANTIC_ENTRY\("([^"]+)",\s*(\w+)\)', src):
        antics.append({"name": m.group(1), "steps": steps_by_array[m.group(2)]})
    return poses, antics


def commands():
    src = read(MAIN / "voice" / "voice_commands.c")
    out = []
    for m in re.finditer(r'EN\(VOICE_ACTION_(\w+),\s*"([^"]+)"\)', src):
        out.append({"action": m.group(1).lower(), "phrase": m.group(2)})
    return out


def fnv1a(text):
    """Same hash as main/audio/audio_voice_hash.c - names the offline clips."""
    value = 0x811C9DC5
    for byte in text.encode("utf-8"):
        value = ((value ^ byte) * 0x01000193) & 0xFFFFFFFF
    return "%08x" % value


def c_strings(fragment):
    """Every string in a C initialiser, joining adjacent literals."""
    fragment = re.sub(r"/\*.*?\*/", "", fragment, flags=re.S)
    literal = r'"((?:[^"\\]|\\.)*)"'
    out = []
    for run in re.finditer(r"%s(?:\s*%s)*" % (literal, literal), fragment):
        text = "".join(re.findall(literal, run.group(0)))
        out.append(text.encode("utf-8").decode("unicode_escape").encode("latin-1").decode("utf-8"))
    return out


def replies():
    """Fixed replies per action: the PICK() tables and the emote table.

    Replies the firmware builds at runtime (time, temperature, timer, sums,
    uptime...) have no fixed text, so they are listed with runtime=True and
    the simulator says so instead of inventing an answer.
    """
    src = read(MAIN / "voice" / "voice_local_brain.c")
    voice_dir = ROOT / "spiffs" / "voice"
    tables = {m.group(1): c_strings(m.group(2)) for m in re.finditer(
        r"static const char \*const (s_\w+)\[\] = \{(.*?)\n\};", src, re.S)}

    def line(text):
        clip = fnv1a(text)
        return {"text": text, "clip": clip if (voice_dir / (clip + ".mp3")).exists() else None}

    out = {}
    emote = src.split("s_emote_requests[] = {", 1)[1].split("\n};", 1)[0]
    for m in re.finditer(r'\{\s*VOICE_ACTION_(\w+),\s*FACE_EMOTION_(\w+),\s*"([^"]*)",\s*(true|false)\s*\}',
                         emote):
        out[m.group(1).lower()] = {"emotion": m.group(2).lower(), "antic": m.group(4) == "true",
                                   "lines": [line(m.group(3))], "runtime": False}

    switch = src.split("switch (action) {", 1)[1]
    for m in re.finditer(r"case VOICE_ACTION_(\w+):(.*?)break;", switch, re.S):
        body = m.group(2)
        action = m.group(1).lower()
        pick = re.search(r"PICK\((s_\w+)\)", body)
        emo = re.search(r"reply->emotion = FACE_EMOTION_(\w+);", body)
        if pick and emo and pick.group(1) in tables:
            out[action] = {"emotion": emo.group(1).lower(), "antic": "play_antic = true" in body,
                           "lines": [line(t) for t in tables[pick.group(1)]], "runtime": False}
        elif action not in out:
            out[action] = {"emotion": None, "antic": False, "lines": [], "runtime": True}
    return out


def build():
    firmware, geometry, scale = config_and_geometry()
    geometry.update(view_constants())
    poses, antics = emotions(scale)
    data = {
        "_generated_by": "tools/extract_firmware_data.py - do not edit by hand",
        "firmware": firmware,
        "theme": theme(),
        "geometry": geometry,
        "emotions": poses,
        "antics": antics,
        "commands": commands(),
        "replies": replies(),
        "voice_clip_count": len(list((ROOT / "spiffs" / "voice").glob("*.mp3"))),
    }
    as_json = json.dumps(data, indent=1, sort_keys=False) + "\n"
    as_js = ("/* Generated by tools/extract_firmware_data.py from the firmware source."
             " Do not edit. */\nwindow.FIRMWARE = " + as_json.rstrip() + ";\n")
    return as_json, as_js, data


def main():
    as_json, as_js, data = build()
    if "--check" in sys.argv:
        stale = [p for p, text in ((OUT_JSON, as_json), (OUT_JS, as_js))
                 if not p.exists() or p.read_text(encoding="utf-8") != text]
        if stale:
            print("stale: " + ", ".join(str(p.relative_to(ROOT)) for p in stale))
            print("run: python tools/extract_firmware_data.py")
            return 1
        print("firmware-data up to date")
        return 0
    OUT_JS.parent.mkdir(parents=True, exist_ok=True)
    OUT_JSON.write_text(as_json, encoding="utf-8", newline="\n")
    OUT_JS.write_text(as_js, encoding="utf-8", newline="\n")
    print("%d expressions, %d antics, %d English phrases -> %s"
          % (len(data["emotions"]), len(data["antics"]), len(data["commands"]),
             OUT_JS.relative_to(ROOT)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
