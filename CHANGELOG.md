# Changelog

## 2026-10-21 · repository created

- Imported Desk Buddy firmware v3.7.1 (Unlicense / CC0) with the BOX-3 board support component (Apache-2.0) and the pinned `dependencies.lock`.
- Verified `idf.py build` on ESP-IDF v5.5.5 (see `docs/BUILD_RESULTS.md`).
- Added the web simulator (`web/`), generated from the firmware tables by `tools/extract_firmware_data.py`.
- Added README art rendered from the same tables by `tools/render_readme_art.py`.
- Added CI: firmware build in the official ESP-IDF v5.5.5 container, host tests, data freshness, GitHub Pages deploy.
- `spiffs/openai_key.txt` is no longer shipped; use `spiffs/openai_key.example.txt`.
