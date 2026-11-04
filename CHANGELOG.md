# Changelog

## Unreleased · More expressive face

- **Face marks**: a floating "Z z" when asleep, a bead of sweat (nervous, exhausted on low battery), a tear (sad), "?" (confused), "!" (shocked when the timer ends, surprised when picked up), a heart (love). Built from two strokes and a dot each; no images, no fonts, no layer buffers.
- **Muted face**: the mute switch now shows on the screen. Soft eyes and stitched lips while muted, with a caption, and the face holds still until the switch goes back. `muted` cannot be set by a cloud reply.
- **Moods fade**: a reply's expression relaxes to neutral after 90 s alone, then sleepy at 5 min, then asleep at 15 min. Device-state faces (listening, thinking, speaking, muted) never drift.
- **Tickles**: three quick taps start a giggle fit; seven make the buddy scrunch up and give in.
- Three new antics: daydream, aha, giggle fit.
- Simulator mirrors all of it (marks, mute face, drift, tickles), generated from the firmware tables.
- New host tests for marks, mood drift and tickle detection.
- **Untested on device.**

## Unreleased · Beginner guide and secure development

- New [docs/GETTING_STARTED.md](docs/GETTING_STARTED.md): install, build, find the port, flash, recover and change the code on Windows, macOS and Linux, with official download links, a troubleshooting section and accessibility notes. macOS and non-Ubuntu Linux steps follow Espressif's v5.5.5 instructions and are **not yet walked through** for this project.
- New [docs/GITHUB_SECURITY.md](docs/GITHUB_SECURITY.md): account and repository settings checklist (2FA, secret scanning, push protection, Dependabot, rulesets, Actions permissions).
- `scripts/check_secrets.py` (standard library only) replaces the inline `git grep` secret checks in CI and the release workflow. It no longer mistakes words like `desk-buddy` for `sk-` keys, also catches GitHub/AWS/Google/Slack tokens, private keys and Wi-Fi passwords, and refuses files such as `sdkconfig` and `.env`. Tests in `tests/test_check_secrets.py`.
- `.githooks/pre-commit` runs the same check before every commit (`git config core.hooksPath .githooks`); `.pre-commit-config.yaml` for pre-commit.com users.
- `.gitignore` covers `sdkconfig.*` variants, loose firmware images, ELF/map files, more key formats, flash-encryption and signing keys, and virtualenvs.
- `.github/dependabot.yml` for GitHub Actions and the desktop app's npm packages.

## Unreleased · More offline phrases

- 20 new offline phrases (127 to 147 English), kept under the ~150 internal-RAM ceiling agreed with the boot-memory work: new wordings for time, date, jokes, volume, goodbyes, thanks, compliments and cheering up, plus timers for three and fifteen minutes.
- New offline answers: riddles, "say that again" (repeats the last reply word for word, kept in PSRAM), "never mind", an honest "what is the weather like" (no forecast, points at the room sensor), favourite colour, "i am so bored", and screen brightness up/down by voice.
- More variety in existing replies; the "there is no battery in me" fact now allows for the dock battery.
- 37 new voice clips (about 290 KB); the storage image was checked with spiffsgen and still fits with about 95 KB spare.
- **Untested on device.**

## Unreleased · Home AI brain mode

- New **Home AI** brain mode: unmatched questions go to `tools/home_brain/home_brain.py` on a computer on the LAN (Whisper + llama3.2 via Ollama + local TTS), with no OpenAI key, account or bill. Falls back to offline answers when it is not set up; a 1.5 s health probe catches a switched-off PC. **Untested on device.**
- Settings layout v8 (`home_brain_url`), migrated from v7 with every field kept.
- Router tests for the new mode; wire-format tests for the home brain server.

## 2026-10-21 · repository created

- Imported Desk Buddy firmware v3.7.1 (Unlicense / CC0) with the BOX-3 board support component (Apache-2.0) and the pinned `dependencies.lock`.
- Verified `idf.py build` on ESP-IDF v5.5.5 (see `docs/BUILD_RESULTS.md`).
- Added the web simulator (`web/`), generated from the firmware tables by `tools/extract_firmware_data.py`.
- Added README art rendered from the same tables by `tools/render_readme_art.py`.
- Added CI: firmware build in the official ESP-IDF v5.5.5 container, host tests, data freshness, GitHub Pages deploy.
- `spiffs/openai_key.txt` is no longer shipped; use `spiffs/openai_key.example.txt`.
