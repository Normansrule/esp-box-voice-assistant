# Roadmap

Labels: **VERIFIED ON DEVICE**, **VERIFIED IN TEST**, **VERIFIED BUILD**, **SIMULATED**, **UNTESTED**, **PLANNED**.
Nothing moves to "verified on device" without serial logs or measurements committed under `docs/`.

| Phase | Item | State |
| --- | --- | --- |
| 1 Hardware audit | Confirm BOX-3 variant (16 MB flash / 16 MB PSRAM), board revision, sensor dock revision | PLANNED |
| 1 Hardware audit | Read onboard IMU chip ID (BSP lists ICM42670; confirm, do not assume a Bosch BMI part) | PLANNED |
| 1 Hardware audit | Identify battery hardware: BOX-3-SENSOR dock 18650 holder or other module; charger, protection, power switch, voltage sense | PLANNED |
| 2 Bring-up | Firmware builds on ESP-IDF v5.5.5 | VERIFIED BUILD |
| 2 Bring-up | Flash, boot self-test log, touch, mic, speaker on this repo's build | UNTESTED |
| 3 Offline voice | Wake word, 147 phrases, offline clips, mute switch | in firmware, UNTESTED for this repo |
| 4 Portable UX | Battery voltage via GPIO10 (verified sense line on SENSOR-01 V1.1), estimated percentage, low-battery alert | IN FIRMWARE, UNTESTED ON DEVICE |
| 4 Portable UX | Sleep profiles, battery-life measurements | PLANNED |
| 4 Portable UX | IMU at 50 Hz, rest-pose zeroing, tilt gaze, shake, pick-up/set-down | IN FIRMWARE, UNTESTED ON DEVICE |
| 4 Portable UX | Double-tap, sensitivity settings, false-trigger tuning from logs | PLANNED |
| 4 Portable UX | Developer screen: heap/PSRAM, audio drops, reset reason, IMU ID, power; UNKNOWN when unavailable | PLANNED |
| 5 Optional AI | Consent screen before any audio leaves the device; visible cloud indicator | PLANNED |
| 5 Optional AI | Move from a device-held OpenAI key to a user-owned backend so no service secret lives in flash | PLANNED |
| 6 Release | CI: firmware build, host tests, data/site freshness, Pages | in `.github/workflows/` |

## Battery safety rules

- Never connect or charge a lithium cell through a module whose charger, protection and polarity have not been checked against its schematic.
- Battery percentage is shown as **estimated** unless a validated fuel gauge exists.
- Undervoltage cut-off and safe shutdown are designed from the real schematic, not guessed.
