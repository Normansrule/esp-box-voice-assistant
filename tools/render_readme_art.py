#!/usr/bin/env python3
"""
render_readme_art.py - draw the README's pictures from the firmware's own tables.

SPDX-License-Identifier: Unlicense OR CC0-1.0

Reads web/assets/firmware-data.json (run tools/extract_firmware_data.py first)
and writes SVGs into docs/assets/:

    hero.svg           animated banner: the face blinking between expressions
    expressions.svg    every expression in s_emotion_poses, labelled
    voice-states.svg   the conversation flow from voice_assistant.c
    architecture.svg   the layers under main/

The face drawing is a port of main/face/face_view.c (same as web/assets/face.js),
so each render is what the 320x240 panel shows for that pose, not artwork.
"""

import json
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DATA = json.loads((ROOT / "web" / "assets" / "firmware-data.json").read_text(encoding="utf-8"))
OUT = ROOT / "docs" / "assets"
G = DATA["geometry"]
T = DATA["theme"]
POSES = {e["name"]: e["pose"] for e in DATA["emotions"]}
RED, BG, BLUSH = T["espressif_red"], T["background"], T["blush"]
TEXT, MUTED, SURFACE, BORDER = T["text"], T["text_muted"], T["surface"], T["surface_border"]
FONT = "Inter, -apple-system, 'Segoe UI', Helvetica, Arial, sans-serif"
MONO = "'JetBrains Mono', SFMono-Regular, Consolas, monospace"


def div(a, b):
    q = abs(a) // abs(b)
    return q if (a >= 0) == (b >= 0) else -q


def clamp(v, lo, hi):
    return lo if v < lo else hi if v > hi else v


_uid = [0]


def uid():
    _uid[0] += 1
    return "c%d" % _uid[0]


def face_svg(pose, shift=0):
    """Return SVG elements for one pose, in 320x240 screen coordinates."""
    out = []
    fdx = pose["face_offset_x"]
    fdy = pose["face_offset_y"] + shift
    ldx = div(clamp(pose["look_x"], -100, 100) * G["FACE_EYE_LOOK_RANGE_X"], 100)
    ldy = div(clamp(pose["look_y"], -100, 100) * G["FACE_EYE_LOOK_RANGE_Y"], 100)
    cx0 = G["BUDDY_SCREEN_WIDTH"] // 2

    # Blush (behind everything)
    if pose["blush_opacity"] > 0:
        d = G["FACE_BLUSH_DIAMETER"]
        for sign in (-1, 1):
            x = cx0 + sign * G["FACE_BLUSH_OFFSET_X"] + fdx + div(ldx, 2) - div(d, 2)
            y = G["FACE_BLUSH_OFFSET_Y"] + fdy + div(ldy, 2) - div(d, 2)
            out.append('<circle cx="%g" cy="%g" r="%g" fill="%s" fill-opacity="%.3f"/>'
                       % (x + d / 2, y + d / 2, d / 2, BLUSH, pose["blush_opacity"] / 255))

    # Eyes with lid cutters
    for side, shut in ((0, pose["left_eye_shut"]), (1, pose["right_eye_shut"])):
        w = clamp(pose["eye_width"], 8, 160)
        h = G["EYE_SHUT_HEIGHT"] if shut else clamp(pose["eye_height"], 4, 200)
        r = div(G["EYE_SHUT_HEIGHT"], 2) if shut else pose["eye_corner_radius"]
        lower = G["EYE_SHUT_CRESCENT"] if shut else pose["eye_lower_lid"]
        upper = 0 if shut else pose["eye_upper_lid"]
        r = clamp(r, 0, div(min(w, h), 2))
        lower, upper = clamp(lower, 0, h), clamp(upper, 0, h)
        spacing = -G["FACE_EYE_SPACING_FROM_CENTRE"] if side == 0 else G["FACE_EYE_SPACING_FROM_CENTRE"]
        cx = cx0 + spacing + fdx + ldx
        cy = G["FACE_EYE_CENTRE_Y"] + fdy + ldy
        x, y = cx - div(w, 2), cy - div(h, 2)
        cid = uid()
        cut = max(w * 2, 48)
        ccx = x + div(w - cut, 2) + cut / 2
        # The lids are holes cut out of the eye (a mask), not white paint on
        # top of it, so no red fringe survives along the cut.
        out.append('<mask id="%s" maskUnits="userSpaceOnUse" x="%d" y="%d" width="%d" height="%d">'
                   '<rect x="%d" y="%d" width="%d" height="%d" fill="#fff"/>'
                   '<circle cx="%g" cy="%g" r="%g" fill="#000"/><circle cx="%g" cy="%g" r="%g" fill="#000"/></mask>'
                   % (cid, x, y, w, h, x, y, w, h, ccx, y + h - lower + cut / 2, cut / 2,
                      ccx, y + upper - cut + cut / 2, cut / 2))
        out.append('<rect x="%d" y="%d" width="%d" height="%d" rx="%d" fill="%s" mask="url(#%s)"/>'
                   % (x, y, w, h, r, RED, cid))

    # Brows
    if pose["brows_visible"]:
        tilt = clamp(pose["brow_inner_tilt"], -16, 16)
        hp = div(G["FACE_BROW_THICKNESS"], 2)
        far, mid = hp + G["FACE_BROW_LENGTH"], hp + div(G["FACE_BROW_LENGTH"], 2)
        by = G["BROW_BASELINE_Y"]
        midy = by + div(tilt, 2) - G["BROW_ARCH"]
        pts = [[(hp, by), (mid, midy), (far, by + tilt)], [(hp, by + tilt), (mid, midy), (far, by)]]
        base = (G["FACE_EYE_CENTRE_Y"] - div(G["FACE_EYE_HEIGHT"], 2) - G["FACE_BROW_GAP_ABOVE_EYE"]
                + pose["brow_offset_y"] + fdy + div(ldy, 2))
        base = max(base, G["FACE_BROW_MIN_BASELINE_Y"])
        cw = G["FACE_BROW_LENGTH"] + G["FACE_BROW_THICKNESS"]
        for side in (0, 1):
            spread = G["FACE_EYE_SPACING_FROM_CENTRE"] + pose["brow_spread"]
            cx = cx0 + (-spread if side == 0 else spread) + fdx + div(ldx, 2)
            ox, oy = cx - div(cw, 2), base - by + (pose["brow_asymmetry"] if side == 0 else 0)
            out.append('<polyline points="%s" fill="none" stroke="%s" stroke-width="%d" '
                       'stroke-linecap="round" stroke-linejoin="round"/>'
                       % (" ".join("%d,%d" % (ox + px, oy + py) for px, py in pts[side]), RED,
                          G["FACE_BROW_THICKNESS"]))

    # Mouth
    w = clamp(pose["mouth_width"], 16, G["MOUTH_CANVAS_WIDTH"] - 20)
    curve = clamp(pose["mouth_curve"], -34, 34)
    opening = clamp(pose["mouth_open_height"], 0, G["FACE_MOUTH_MAX_OPEN_HEIGHT"])
    cx = cx0 + fdx + div(ldx, 3)
    cy = G["FACE_MOUTH_CENTRE_Y"] + pose["mouth_offset_y"] + fdy + div(ldy, 3)
    if opening > G["FACE_MOUTH_THICKNESS"]:
        out.append('<rect x="%d" y="%d" width="%d" height="%d" rx="%g" fill="%s"/>'
                   % (cx - div(w, 2), cy + curve - div(opening, 2), w, opening, min(w, opening) / 2, RED))
    else:
        last = G["FACE_MOUTH_POINT_COUNT"] - 1
        mw, mb = G["MOUTH_CANVAS_WIDTH"], G["MOUTH_BASELINE_Y"]
        ox, oy = cx - div(mw, 2), cy - mb
        pts = []
        for i in range(last + 1):
            u = 2 * i - last
            px = div(mw, 2) - div(w, 2) + div(w * i, last)
            py = mb + curve - div(curve * u * u, last * last)
            pts.append("%d,%d" % (ox + px, oy + py))
        out.append('<polyline points="%s" fill="none" stroke="%s" stroke-width="%d" '
                   'stroke-linecap="round" stroke-linejoin="round"/>'
                   % (" ".join(pts), RED, G["FACE_MOUTH_THICKNESS"]))
    return "".join(out)


def screen(x, y, scale, pose, label=None, radius=14, shadow=True):
    s = ['<g transform="translate(%g %g)">' % (x, y)]
    w, h = 320 * scale, 240 * scale
    if shadow:
        s.append('<rect x="0" y="6" width="%g" height="%g" rx="%g" fill="%s" opacity=".10" filter="url(#blur)"/>'
                 % (w, h, radius, RED))
    s.append('<rect width="%g" height="%g" rx="%g" fill="#fff" stroke="%s"/>' % (w, h, radius, BORDER))
    cid = uid()
    s.append('<clipPath id="%s"><rect width="%g" height="%g" rx="%g"/></clipPath>' % (cid, w, h, radius))
    s.append('<g clip-path="url(#%s)"><g transform="scale(%g)">%s</g></g>' % (cid, scale, face_svg(pose)))
    if label:
        s.append('<text x="%g" y="%g" text-anchor="middle" font-family="%s" font-size="13" '
                 'font-weight="600" fill="%s">%s</text>' % (w / 2, h + 22, FONT, TEXT, label))
    s.append("</g>")
    return "".join(s)


DEFS = ('<defs><filter id="blur" x="-20%" y="-20%" width="140%" height="160%">'
        '<feGaussianBlur stdDeviation="10"/></filter>'
        '<radialGradient id="glow" cx="50%" cy="50%" r="50%"><stop offset="0" stop-color="#E8362D" stop-opacity=".22"/>'
        '<stop offset="1" stop-color="#E8362D" stop-opacity="0"/></radialGradient></defs>')


def svg(w, h, body, title):
    return ('<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 %d %d" width="%d" height="%d" '
            'role="img" aria-label="%s"><title>%s</title>%s%s</svg>\n' % (w, h, w, h, title, title, DEFS, body))


def hero():
    """Banner whose face cross-fades through expressions and blinks (CSS, no JS)."""
    W, H = 1280, 520
    seq = ["neutral", "happy", "curious", "wink", "love", "starry", "playful"]
    n = len(seq)
    dur = n * 2.4
    frames = []
    for i, name in enumerate(seq):
        frames.append('<g class="f f%d">%s</g>' % (i, face_svg(POSES[name])))
    keyframes = []
    for i in range(n):
        a = i / n * 100
        b = (i + 1) / n * 100
        keyframes.append("@keyframes k%d{0%%{opacity:0}%.2f%%{opacity:0}%.2f%%{opacity:1}%.2f%%{opacity:1}"
                         "%.2f%%{opacity:0}100%%{opacity:0}}.f%d{animation:k%d %gs infinite}"
                         % (i, max(0, a - 1.5), a + 1.5, b - 1.5, min(100, b + 1.5), i, i, dur))
    if True:  # the first frame is also the static fallback
        keyframes.append(".f0{opacity:1}")
    style = ("<style>.f{opacity:0}%s@media (prefers-reduced-motion:reduce){.f{animation:none}.f0{opacity:1}}</style>"
             % "".join(keyframes))
    sx, sy, sc = 760, 92, 1.36
    sw, sh = 320 * sc, 240 * sc
    body = [style,
            '<rect width="%d" height="%d" fill="#fff"/>' % (W, H),
            '<ellipse cx="%g" cy="%g" rx="420" ry="260" fill="url(#glow)"/>' % (sx + sw / 2, sy + sh / 2),
            '<rect x="%g" y="%g" width="%g" height="%g" rx="44" fill="#ECEDF0" stroke="#D8D9DE"/>'
            % (sx - 34, sy - 40, sw + 68, sh + 86),
            '<rect x="%g" y="%g" width="%g" height="%g" rx="18" fill="#0d0e10"/>' % (sx - 12, sy - 12, sw + 24, sh + 24),
            '<circle cx="%g" cy="%g" r="4" fill="#BFC1C7"/><circle cx="%g" cy="%g" r="4" fill="#BFC1C7"/>'
            % (sx + sw * 0.4, sy - 26, sx + sw * 0.6, sy - 26),
            '<svg x="%g" y="%g" width="%g" height="%g" viewBox="0 0 320 240"><rect width="320" height="240" fill="#fff"/>%s</svg>'
            % (sx, sy, sw, sh, "".join(frames)),
            '<text x="%g" y="%g" text-anchor="middle" font-family="%s" font-size="11" letter-spacing="3" '
            'font-weight="700" fill="#A0A3AA">ESP32-S3-BOX-3</text>' % (sx + sw / 2, sy + sh + 32, FONT),
            '<text x="80" y="150" font-family="%s" font-size="17" font-weight="600" fill="%s">'
            'ESP-BOX Voice &#183; Desk Buddy firmware v%s</text>' % (FONT, RED, DATA["firmware"]["version"]),
            '<text x="74" y="290" font-family="%s" font-size="148" font-weight="700" letter-spacing="-6" fill="%s">'
            'Hi <tspan fill="%s">E.S.P.</tspan></text>' % (FONT, TEXT, RED),
            '<text x="80" y="350" font-family="%s" font-size="24" fill="#55595F">A voice assistant with a face.</text>' % FONT,
            '<text x="80" y="384" font-family="%s" font-size="24" fill="#55595F">Wake word and %d phrases on the chip.</text>'
            % (FONT, len(DATA["commands"])),
            '<text x="80" y="418" font-family="%s" font-size="24" fill="#55595F">%d expressions, zero bitmaps.</text>'
            % (FONT, len(DATA["emotions"])),
            ]
    return svg(W, H, "".join(body), "ESP-BOX Voice: Hi E.S.P.")


def expressions():
    cols, sc = 8, 0.42
    cw, ch = 320 * sc + 22, 240 * sc + 52
    rows = (len(DATA["emotions"]) + cols - 1) // cols
    W, H = int(cols * cw + 40), int(rows * ch + 110)
    body = ['<rect width="%d" height="%d" fill="#fff"/>' % (W, H),
            '<text x="30" y="46" font-family="%s" font-size="24" font-weight="700" fill="%s">%d expressions</text>'
            % (FONT, TEXT, len(DATA["emotions"])),
            '<text x="30" y="72" font-family="%s" font-size="13" fill="%s">Rendered from s_emotion_poses in '
            'main/face/face_emotions.c by tools/render_readme_art.py</text>' % (FONT, MUTED)]
    for i, e in enumerate(DATA["emotions"]):
        x = 30 + (i % cols) * cw
        y = 96 + (i // cols) * ch
        body.append(screen(x, y, sc, e["pose"], e["name"], radius=10, shadow=False))
    return svg(W, H, "".join(body), "All Desk Buddy expressions")


def voice_states():
    W, H = 1280, 640
    sc = 0.5
    nodes = [  # id, x, y, title, pose, caption, source
        ("IDLE", 40, 70, "neutral", "Blinks, fidgets, gets sleepy after 5 min", "face_animator.c"),
        ("LISTENING", 360, 70, "listening", "“Hi E.S.P.” heard: chirp, 6 s window", "handle_wake_word()"),
        ("UNDERSTOOD", 680, 70, "happy", "MultiNet matched an offline phrase", "handle_recognised_command()"),
        ("SPEAKING", 1000, 70, "speaking", "Clip from flash, lip sync from loudness", "lip_sync_timer_cb()"),
        ("THINKING", 680, 360, "thinking", "Router allowed one cloud request", "voice_router_decide()"),
        ("CONFUSED", 360, 360, "confused", "“I did not hear anything.”", "VOICE_ROUTE_NOTHING_HEARD"),
        ("DIZZY", 1000, 360, "dizzy", "Request failed or timed out (25 s)", "cloud_failed()"),
    ]
    pos = {}
    body = ['<rect width="%d" height="%d" fill="#fff"/>' % (W, H),
            '<defs><marker id="ar" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" '
            'orient="auto-start-reverse"><path d="M0,0L10,5L0,10z" fill="%s"/></marker>'
            '<marker id="ag" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" '
            'orient="auto-start-reverse"><path d="M0,0L10,5L0,10z" fill="%s"/></marker></defs>' % (RED, MUTED)]
    sw, sh = 320 * sc, 240 * sc
    for nid, x, y, emo, cap, src in nodes:
        pose = dict(POSES[emo])
        if nid == "SPEAKING":
            pose["mouth_open_height"] = 22
        pos[nid] = (x, y)
        body.append(screen(x + 40, y + 34, sc, pose, None, radius=12))
        body.append('<text x="%g" y="%g" font-family="%s" font-size="13" font-weight="700" letter-spacing="1.5" '
                    'fill="%s">%s</text>' % (x + 40, y + 20, FONT, RED, nid))
        body.append('<text x="%g" y="%g" font-family="%s" font-size="13" fill="%s">%s</text>'
                    % (x + 40, y + 34 + sh + 24, FONT, TEXT, cap))
        body.append('<text x="%g" y="%g" font-family="%s" font-size="11" fill="%s">%s</text>'
                    % (x + 40, y + 34 + sh + 42, MONO, MUTED, src))

    def arrow(x1, y1, x2, y2, label, grey=False, curve=0):
        mx, my = (x1 + x2) / 2, (y1 + y2) / 2 + curve
        body.append('<path d="M%g %g Q%g %g %g %g" fill="none" stroke="%s" stroke-width="2" %s marker-end="url(#%s)"/>'
                    % (x1, y1, mx, my, x2, y2, MUTED if grey else RED,
                       'stroke-dasharray="5 5"' if grey else "", "ag" if grey else "ar"))
        if label:
            body.append('<text x="%g" y="%g" text-anchor="middle" font-family="%s" font-size="11.5" fill="%s">%s</text>'
                        % (mx, (y1 + 2 * my + y2) / 4 - 8, FONT, MUTED, label))

    cy = 70 + 34 + sh / 2
    arrow(40 + 40 + sw + 8, cy, 360 + 40 - 8, cy, "wake word")
    arrow(360 + 40 + sw + 8, cy, 680 + 40 - 8, cy, "phrase matched")
    arrow(680 + 40 + sw + 8, cy, 1000 + 40 - 8, cy, "reply has a clip")
    arrow(1000 + 40 + sw / 2, 74, 40 + 40 + sw / 2, 74, "clip ends: back to the reply's face", curve=-60)
    arrow(360 + 40 + sw / 2, 70 + 34 + sh + 52, 360 + 40 + sw / 2, 360 + 4, "6 s, nothing heard", grey=True)
    arrow(360 + 40 + sw - 10, 70 + 34 + sh + 52, 680 + 40 + 10, 360 + 4, "no match, router says ask", grey=True)
    arrow(680 + 40 + sw + 8, 360 + 34 + sh / 2, 1000 + 40 - 8, 70 + 34 + sh - 10, "answer + voice")
    arrow(680 + 40 + sw + 8, 360 + 34 + sh / 2 + 30, 1000 + 40 - 8, 360 + 34 + sh / 2 + 30, "error", grey=True)
    body.append('<text x="40" y="%d" font-family="%s" font-size="12.5" fill="%s">Faces are the real poses each '
                'branch sets in main/voice/voice_assistant.c. Mute switch: capture returns silence, so no branch '
                'ever starts. Dashed = failure or optional path.</text>' % (H - 22, FONT, MUTED))
    return svg(W, H, "".join(body), "Voice states of the Desk Buddy firmware")


def architecture():
    W, H = 1280, 560
    layers = [
        ("main.c", "Start-up order, and nothing else", ["main.c", "buddy_config.h"]),
        ("voice/", "Listening and answering", ["voice_assistant", "voice_wakeword", "voice_router",
                                               "voice_local_brain", "voice_cloud_brain", "voice_recorder"]),
        ("ui/ + face/", "Screens and the face", ["ui_screen_face", "ui_settings", "ui_speech_bubble",
                                                 "face_animator", "face_view", "face_emotions"]),
        ("audio/ + net/ + core/", "Sound, network, settings", ["audio_music", "audio_voice", "audio_prompts",
                                                               "net_wifi", "net_time", "buddy_settings"]),
        ("hardware/", "Talks to chips, knows no behaviour", ["hw_display", "hw_speaker", "hw_microphone",
                                                             "hw_sensors", "hw_buttons"]),
        ("components/bsp + managed", "ESP-BSP, ESP-SR 1.4, LVGL 8, esp-audio-player, openai 1.0",
         ["ESP32-S3-BOX-3", "ES8311 / ES7210 codecs", "ILI9341-class LCD + touch", "sensor dock"]),
    ]
    body = ['<rect width="%d" height="%d" fill="#fff"/>' % (W, H)]
    y = 30
    for i, (name, desc, mods) in enumerate(layers):
        top = i == 0
        fill = RED if top else (SURFACE if i % 2 else "#FAFAFB")
        body.append('<rect x="40" y="%d" width="%d" height="72" rx="18" fill="%s" stroke="%s"/>'
                    % (y, W - 80, fill, RED if top else BORDER))
        body.append('<text x="68" y="%d" font-family="%s" font-size="17" font-weight="700" fill="%s">%s</text>'
                    % (y + 32, MONO, "#fff" if top else RED, name))
        body.append('<text x="68" y="%d" font-family="%s" font-size="13" fill="%s">%s</text>'
                    % (y + 54, FONT, "#ffe3e0" if top else MUTED, desc))
        x = 420
        for m in mods:
            wpx = 14 + len(m) * 7.6
            body.append('<rect x="%g" y="%d" width="%g" height="30" rx="15" fill="#fff" stroke="%s"/>'
                        % (x, y + 21, wpx, "#ffb3ad" if top else BORDER))
            body.append('<text x="%g" y="%d" text-anchor="middle" font-family="%s" font-size="12" fill="%s">%s</text>'
                        % (x + wpx / 2, y + 40, MONO, TEXT, m))
            x += wpx + 8
        y += 84
    body.append('<text x="40" y="%d" font-family="%s" font-size="12.5" fill="%s">Dependencies only point downwards. '
                'Codec and panel names follow the official BOX-3 BSP; verify against your board revision.</text>'
                % (H - 20, FONT, MUTED))
    return svg(W, H, "".join(body), "Firmware architecture")


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    for name, fn in (("hero.svg", hero), ("expressions.svg", expressions),
                     ("voice-states.svg", voice_states), ("architecture.svg", architecture)):
        (OUT / name).write_text(fn(), encoding="utf-8", newline="\n")
        print("wrote docs/assets/" + name)


if __name__ == "__main__":
    main()
