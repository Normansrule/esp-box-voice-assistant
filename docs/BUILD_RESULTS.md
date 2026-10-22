# Build results

Recorded results only. Anything not listed here was not run.

## 2026-10-21 · local build, Windows 11

| Item | Result |
| --- | --- |
| ESP-IDF | v5.5.5 (`C:\Espressif\frameworks\esp-idf-v5.5.5-2`), Python 3.11.2 env |
| Compiler | GNU 14.2.0 (xtensa-esp-elf, from ESP-IDF tools) |
| Commands | `idf.py set-target esp32s3`, `idf.py build`, `idf.py size` |
| Result | **Build passed** |
| App image | `esp_box_desk_buddy.bin` 1,965,360 bytes (0x1dfd30); smallest app partition 0x384000; **47% free** |
| Bootloader | 0x5710 bytes, 32% free |
| Storage image | `storage.bin` 0x3f6000 (SPIFFS: chimes, music, 356 voice clips) |
| Speech models | `srmodels.bin` 8,063,384 bytes |
| DIRAM used | 177,719 of 341,760 bytes (52.0%) |
| Flash code / data | 1,363,634 / 456,604 bytes |
| Warnings | 12, all `-Wunused-function` inside the managed `espressif__openai` component; none in project code |
| Component lock | `dependencies.lock` unchanged by the build (esp-sr, lvgl 8.4.0, esp-box-3 BSP, openai 1.0.x pinned there) |
| `scripts/check_sources.py` | passed (run by `scripts/run_tests.py`) |
| Host unit tests | **not run locally**: no desktop C compiler on this PC. CI runs them on Ubuntu. |
| Flash / on-device | **not run** for this repository. No device logs are claimed. |

## How to reproduce

```bat
idf.py set-target esp32s3
idf.py build
idf.py size
```
