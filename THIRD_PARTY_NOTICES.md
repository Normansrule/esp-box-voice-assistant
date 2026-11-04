# Third-party notices

The project's own code, tools, site and audio are Unlicense OR CC0-1.0 (see [LICENSE](LICENSE)). The firmware images built from this repository, and the ones attached to GitHub releases, also contain the code below. Versions are the ones pinned in [`dependencies.lock`](dependencies.lock).

## In this repository

| Path | Origin | License |
| --- | --- | --- |
| `components/bsp/` | [espressif/esp-box](https://github.com/espressif/esp-box) board support | Apache-2.0 ([LICENSE](components/bsp/LICENSE)) |
| `components/espressif__esp-box-3/` | [espressif/esp-bsp](https://github.com/espressif/esp-bsp/tree/master/bsp/esp-box-3) `esp-box-3` 1.1.3, **modified** (see [DESK_BUDDY_PATCHES.md](components/espressif__esp-box-3/DESK_BUDDY_PATCHES.md)) | Apache-2.0 ([LICENSE](components/espressif__esp-box-3/LICENSE)) |

## Downloaded at build time

These are fetched by the ESP-IDF component manager into `managed_components/` and are not committed. Each keeps its own license, which you can read in `managed_components/<name>/` after a build.

| Component | Version | License | Notes |
| --- | --- | --- | --- |
| `espressif/esp-sr` (WakeNet, MultiNet, AFE, models) | 1.4.2 | ESPRESSIF MIT License | Permission is granted **for use on Espressif products only**. Keep its copyright and permission notice with any copy, including firmware images. |
| `chmorgan/esp-libhelix-mp3` | 1.0.3 | Apache-2.0 (wrapper) | Contains the Helix MP3 decoder from RealNetworks under the **RealNetworks Public Source License (RPSL)**, or the RCSL if you hold one. The decoder is used unmodified; its source is at [chmorgan/libhelix-mp3](https://github.com/chmorgan/libhelix-mp3). |
| `chmorgan/esp-audio-player` | 1.0.5 | see component | |
| `espressif/openai` | 1.0.2 | see component | |
| `espressif/esp_codec_dev` | 1.1.0 | see component | |
| `espressif/esp-dsp` | 1.8.2 | see component | |
| `espressif/esp_lvgl_port` | 1.4.0 | see component | |
| `lvgl/lvgl` | 8.4.0 | see component | |
| `espressif/esp_lcd_ili9341`, `esp_lcd_touch`, `esp_lcd_touch_gt911`, `esp_lcd_touch_tt21100` | 1.2.x | see component | |
| `espressif/icm42670`, `aht20`, `at581x`, `button`, `cmake_utilities` | see lock | see component | |

ESP-IDF itself (v5.5.5) is Apache-2.0 with third-party parts listed in its own repository.

## Tools that made the audio

The voice clips in `spiffs/voice/` are rendered by [eSpeak NG](https://github.com/espeak-ng/espeak-ng) through `scripts/make_voice_clips.py`. eSpeak NG is GPL-3.0; it is a build tool here and is not part of the firmware. Music is synthesised by `scripts/make_music.py` from public-domain scores.

This file is a good-faith summary, not legal advice. If you ship devices, check each component's license text.
