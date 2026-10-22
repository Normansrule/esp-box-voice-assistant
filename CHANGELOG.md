# Changelog

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
