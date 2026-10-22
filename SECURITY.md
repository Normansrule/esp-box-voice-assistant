# Security and privacy

## Reporting

Open a private security advisory on GitHub (Security → Report a vulnerability). Please do not file public issues for vulnerabilities.

## What this firmware does with your data

- **Offline by default.** Wake word and phrase recognition run on the ESP32-S3. The online brain ships switched off (`cloud_brain_enabled = false` in `buddy_settings_reset_to_defaults()`).
- **When you switch the online brain on**, an unrecognised question is recorded into PSRAM and sent over HTTPS to the configured OpenAI-compatible endpoint for transcription, an answer and speech. Recordings are not written to flash.
- **The mute switch** stops capture in the microphone driver; while muted nothing is recognised or sent.
- **An hourly request limit** (default 30) caps cost if the wake word misfires.

## Secrets

- `spiffs/openai_key.txt` is git-ignored. Never commit it, a Wi-Fi password, or a recording.
- The device stores the key **unencrypted** in flash and NVS. Anyone with USB access can read it back. Use a project-scoped key with a low monthly limit.
- The boot log prints the key length, never the key.
- Planned: replace the device-held key with a user-owned backend so no service secret lives on the device.

## Not yet in place

Flash encryption, secure boot and signed OTA are not enabled by this repository's `sdkconfig.defaults`. Enable them before deploying devices you do not physically control.
