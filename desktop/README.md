# Desk Buddy Connect (desktop app)

A Windows app that installs Desk Buddy firmware on an ESP32-S3-BOX-3, shows its
serial log and controls it over USB. It is the page in `web/connect/` (also
published at `/connect/` on the project site) in an Electron window, with
esptool-js and the release firmware bundled so it works offline.

| | Web page | Desktop app |
| --- | --- | --- |
| Needs | Chrome or Edge on a computer | Windows 10/11, x64 |
| Firmware | latest release, or files you pick | the release it shipped with, or files you pick |
| Internet | to load the page and firmware | not needed |

## Get it

Download `Desk-Buddy-Connect-<version>-setup.exe` (installer) or
`-portable.exe` (no install) from the
[releases page](https://github.com/Normansrule/esp-box-voice-assistant/releases).
Builds of the latest commit are under the **Desktop app** workflow's artifacts.

The app is not code-signed, so Windows SmartScreen warns on first run: choose
**More info → Run anyway**.

## Use it

1. Plug the box in with a USB-C **data** cable.
2. **Connect box**, then pick `USB JTAG/serial debug unit` (Espressif ports are listed first).
3. **Install**, **Monitor** or **Control**.

Control needs firmware with the serial console (`main/core/buddy_console.c`,
added after v3.7.1). The commands are listed in `main/core/buddy_console.h` and
work from any serial terminal too:

```
status | selftest | phrases | say <phrase> | face <name> | faces
volume <0-100> | brightness <10-100> | timer <minutes> | timer cancel | reboot | help
```

If the port will not open, close anything else using it (`idf.py monitor`,
Arduino IDE, another browser tab). If the chip will not enter the bootloader,
hold **BOOT**, tap **RESET**, release **BOOT** and try again.

## Build it

```bash
cd desktop
npm install
npm test            # prepares app/ and runs the offline checks
npm start           # run from source
npm run dist:win    # dist/*.exe
```

To bundle firmware, put a release folder (images, `flasher_args.json`,
`manifest.json` from `tools/firmware_manifest.py`) in `desktop/firmware/`
before `npm run dist:win`. The release workflow does this for every tag.

## Security

- The window only loads the bundled page (`app://buddy/connect/`); links open in your browser.
- Serial access is granted to that page only, and only to the port you pick.
- No Node.js in the page: context isolation and the renderer sandbox are on,
  and the preload exposes two functions (`onPortList`, `choosePort`).
- Release images are refused by `tools/firmware_manifest.py` if any contains
  something shaped like an API key.

## Test status

| What | Status |
| --- | --- |
| Page UI, release manifest loading, log colouring, reply parsing | VERIFIED IN TEST (browser, no box, simulated serial text) |
| MD5 used to verify writes | VERIFIED IN TEST (`npm test`, against Node's crypto) |
| Serial console firmware | builds with ESP-IDF v5.5.5; UNTESTED on a device |
| Flashing, monitor, controls on a real BOX-3 | UNTESTED |
| Electron app window and port picker | UNTESTED until the first CI build is run |
