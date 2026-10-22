# Contributing

This repository is maintained by its owner. Issues and suggestions are welcome; pull requests may be closed without merging.

Before proposing a change:

1. `idf.py build` passes on ESP-IDF v5.5.x.
2. `python scripts/run_tests.py` passes (needs a desktop C compiler).
3. If you touched `main/face/`, `main/voice/voice_commands.c`, `voice_local_brain.c` or `buddy_config.h`, run `python tools/extract_firmware_data.py` and `python tools/render_readme_art.py` and commit the results. CI checks they are fresh.
4. Say what you verified and how. Do not describe anything as tested on hardware without logs.
5. No secrets, Wi-Fi credentials or recordings in commits.
