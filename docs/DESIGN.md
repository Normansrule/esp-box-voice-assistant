# Design

One visual world for the box, the simulator site and the browser installer.

## The idea

The buddy lives on a desk. So the screen, the site and the installer are drawn as **ink on warm paper**, with **one vermilion** spent only on the face, on primary actions and on state. At night the same desk gets **Ember**: warm near-black with a brighter face, so the box does not light up the room.

What this replaced, and why: the previous look was pure white with Espressif red everywhere, plus web-template habits (labels above every heading, gradient text, frosted-glass nav, blurred colour blobs, the same fade-in on every section). Two of its colour pairs also failed WCAG contrast on the device (see below).

Guided by the design skills in the project uploads: *impeccable* (craft floor, Operate vs Persuade modes, the list of template habits to refuse), *taste*, the Apple HIG skills (touch targets, legibility at arm's length), and the animation skills (motion conveys state, exits faster than entrances, ease-out).

## Palette

Source of truth: `main/face/face_theme.h`. The site's CSS variables copy the PAPER values and say which define each one mirrors.

| Token | Paper (day) | Ember (night) | Used for |
|---|---|---|---|
| FACE | `#E8362D` | `#FF5B4A` | Eyes, brows, mouth. Never for text. |
| ACCENT | `#C42A20` | `#F25A4C` | Accent text, primary buttons, toggles, urgent states |
| ACCENT_PRESSED | `#9E1F17` | `#B9372B` | Pressed accent |
| BLUSH | `#F3B4AB` | `#6A2A24` | Blush circles; text selection on the site |
| BACKGROUND | `#F6F3EE` | `#13110F` | Screen and page |
| SURFACE | `#EBE6DF` | `#25211D` | Chips, caption sheet, settings rows |
| SURFACE_BORDER | `#D9D2C8` | `#3A342E` | Hairlines, pressed chips |
| TEXT | `#1E1B18` | `#F1ECE5` | Body text |
| TEXT_MUTED | `#5F5952` | `#A39A90` | Secondary text |
| ON_ACCENT | `#FFFFFF` | `#13110F` | Text sitting on ACCENT |

### Contrast (WCAG 2, computed)

| Pair | Paper | Ember | Old palette |
|---|---|---|---|
| Text on background | 15.5 | 16.0 | |
| Muted text on chip surface | 5.6 | 5.8 | **2.9 (fail)** |
| Accent text on caption surface | 4.6 | 4.8 | **3.8 (fail)** |
| Accent text on background | 5.1 | 5.7 | |
| ON_ACCENT on accent | 5.7 | 5.7 | 4.2 (white on old red) |
| Face shapes on background (graphics, 3:1 needed) | 3.8 | 6.1 | |

Known gap: the settings header in `ui_settings.c` draws its title in hard-coded white. On Ember that is 3.3:1. It should use `FACE_COLOR_ON_ACCENT`; left for after the touch-crash work in that file lands.

## Type

- **Device:** Montserrat 12/14/16 (already compiled in). No new fonts in flash.
- **Site:** Bricolage Grotesque for headings, Instrument Sans for text and UI, JetBrains Mono only for code and measured data, Montserrat inside the simulated screen because that is the firmware's font.
- Numbers use tabular figures on the site. On the device the timer pill reserves the width of `00:00`, because Montserrat's digits are proportional and the pill used to twitch every second.

## Shape

Radius steps: 8 (small controls), 12 (chips), 14 (caption sheet, cards), 22 (large panels). Hairline borders instead of glows. Shadows, where they exist on the site, have an offset and a soft blur. The device uses no shadows at all: LVGL shadows cost draw time and RAM.

## Motion

- Motion only ever reports a state change. Nothing animates for decoration.
- Device caption: rises 8 px and fades in over 180 ms (ease-out), leaves in 120 ms. It animates colour alpha and translation only, never LVGL's object-wide `opa`, which would render through an off-screen layer buffer in internal RAM. `CONFIG_BUDDY_UI_REDUCED_MOTION` turns it off.
- Site: one authored moment (the hero title settles, then the 3D device). Sections no longer fade in one by one. `prefers-reduced-motion` turns animations off.

## Choosing the theme on the device

`idf.py menuconfig` → *Desk Buddy appearance* → *Screen theme*: Paper (default) or Ember. `face_theme_set()` exists for a future Settings row; it applies to screens built after the call.

## Memory

Both palettes are `const` (flash). The theme selector is one byte of RAM. Applying the theme re-initialises LVGL's existing default-theme styles in place, with no new allocation. The caption animation allocates one transient `lv_anim_t` from the LVGL heap while it runs.

## Status

| Change | State |
|---|---|
| Palettes, theme selection, caption fade, timer pill | **UNTESTED** on device. Compiles against the project's LVGL 8.4 headers (gcc `-fsyntax-only`, both themes, motion on and off); host tests pass. No `idf.py build` yet. |
| Site and installer restyle | **VERIFIED IN TEST**: rendered in headless Chromium at 1440 px and 390 px, no console errors, no horizontal scroll. |
