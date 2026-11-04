<h1 align="center">Getting started</h1>
<p align="center"><b>Flash it, change it, keep it safe.</b><br>
From an empty computer to your own code running on an ESP32-S3-BOX-3.</p>

<p align="center">
  <img alt="Windows" src="https://img.shields.io/badge/Windows-10%20%7C%2011-1f1f1f?style=flat-square">
  <img alt="macOS" src="https://img.shields.io/badge/macOS-12%2B-1f1f1f?style=flat-square">
  <img alt="Linux" src="https://img.shields.io/badge/Linux-Ubuntu%20%7C%20Fedora%20%7C%20Arch-1f1f1f?style=flat-square">
  <img alt="ESP-IDF v5.5.5" src="https://img.shields.io/badge/ESP--IDF-v5.5.5-E8362D?style=flat-square">
</p>

> **Quick start, if you have done this before:** install ESP-IDF **v5.5.5**,
> clone into a path **without spaces**, then
> `idf.py set-target esp32s3 && idf.py build && idf.py -p <port> flash monitor`.
> Turn on the secret check with `git config core.hooksPath .githooks`.

This guide takes you from "I have never programmed a microcontroller" to
"I changed the code and it is running on my box", on **Windows, macOS or
Linux**. Follow the parts in order the first time. Every command is written
out in full; copy and paste them rather than retyping.

**What has been checked, honestly:** the firmware build with ESP-IDF v5.5.5 is
verified on Windows (the maintainer's machine) and on Ubuntu (CI, on every
push). Flashing over USB is verified on Windows. The macOS and other Linux
steps follow Espressif's official instructions for the same ESP-IDF version but
have not been walked through on those systems for this project yet. The web
flasher has been tested in a browser but **not yet against a real box**. If a
step here does not match what you see, please
[open an issue](https://github.com/Normansrule/esp-box-voice-assistant/issues)
and say which system you are on.

---

## Contents

- [0. Pick your route](#0-pick-your-route)
- [1. What you need](#1-what-you-need)
- [2. Install the tools](#2-install-the-tools)
  - [Windows](#windows)
  - [macOS](#macos)
  - [Linux](#linux)
  - [Any system: VS Code instead of a terminal](#any-system-vs-code-instead-of-a-terminal)
- [3. Get the code](#3-get-the-code)
- [4. Build](#4-build)
- [5. Find your box's port (`ESP_PORT`)](#5-find-your-boxs-port-esp_port)
- [6. Flash and watch it start](#6-flash-and-watch-it-start)
- [7. First setup on the box](#7-first-setup-on-the-box)
- [8. Change the code](#8-change-the-code)
- [9. Keep your secrets out of git](#9-keep-your-secrets-out-of-git)
- [10. Put your changes on GitHub](#10-put-your-changes-on-github)
- [11. Recover the box, or go back to the factory firmware](#11-recover-the-box-or-go-back-to-the-factory-firmware)
- [12. Troubleshooting: every error we know about](#12-troubleshooting-every-error-we-know-about)
- [13. Accessibility and other setups](#13-accessibility-and-other-setups)
- [Glossary](#glossary)

---

## 0. Pick your route

| I want to... | Do this |
| --- | --- |
| just try it, no coding | [Web flasher](https://normansrule.github.io/esp-box-voice-assistant/connect/) in Chrome or Edge. Needs firmware files (see [6c](#6c-the-web-flasher-no-install)). |
| change the code, comfortable with a terminal | Parts 1 to 10 below |
| change the code, prefer clicking buttons | Part 1, then [VS Code](#any-system-vs-code-instead-of-a-terminal), then part 3 onwards |
| undo everything | [Part 11](#11-recover-the-box-or-go-back-to-the-factory-firmware) |

---

## 1. What you need

- An **ESP32-S3-BOX-3** (or BOX-3B). The sensor dock is optional.
- A **USB-C data cable**. Many cheap cables only charge. If your computer never
  notices the box, try another cable before anything else.
- A computer with **about 5 GB of free disk space** and an internet connection
  for the first build. Windows 10 or 11, macOS 12 or newer, or a recent 64-bit
  Linux.
- 30 to 60 minutes the first time. The first build alone takes 5 to 10 minutes.

You do **not** need a USB driver for the BOX-3. It talks over the ESP32-S3's
own USB port, which Windows 10/11, macOS and Linux recognise out of the box.
(Driver downloads such as the
[Silicon Labs CP210x](https://www.silabs.com/developer-tools/usb-to-uart-bridge-vcp-drivers)
are only for *other* ESP32 boards that have a separate USB-serial chip.)

**The one version rule:** this project needs **ESP-IDF v5.5.x** (it is pinned
to **v5.5.5** in `dependencies.lock` and CI). ESP-IDF v6 removed a driver the
sensor dock code uses, so v6 will not build it.

---

## 2. Install the tools

ESP-IDF is Espressif's free toolkit: the compiler, the build system (`idf.py`)
and the flashing tool. Pick your system.

### Windows

1. **Download the ESP-IDF installer** from Espressif:
   **<https://dl.espressif.com/dl/esp-idf/>**
   Choose the **Offline installer for ESP-IDF v5.5.5** if it is listed (newest
   v5.5.x otherwise). The offline one is large but needs no internet while it
   runs and includes Git and Python. The online installer also works; pick
   **v5.5.5** in its version list.
2. **Run it.** Accept the defaults, with two exceptions:
   - Install somewhere short with **no spaces**, for example `C:\Espressif`.
     ESP-IDF refuses paths with spaces or brackets, and paths longer than 90
     characters.
   - When it asks about drivers, you can leave them ticked; the BOX-3 does not
     need them but they do no harm.
3. When it finishes, the Start menu has two new shortcuts:
   **ESP-IDF 5.5 CMD** and **ESP-IDF 5.5 PowerShell**. **Every command in this
   guide must be typed in one of those windows.** A normal Command Prompt,
   PowerShell, Git Bash or WSL window does not know what `idf.py` is.
4. Check it worked. In the ESP-IDF window:

   ```bat
   idf.py --version
   ```

   You should see `ESP-IDF v5.5.5` (or another v5.5.x).

> **Do not use WSL (Ubuntu on Windows) for this.** It was the first thing that
> went wrong when this project was set up: WSL cannot see the box's USB port
> without extra tools, and it has its own separate ESP-IDF install. Use the
> ESP-IDF 5.5 shortcut. See [troubleshooting](#idfpy-in-wsl-or-git-bash).

Optional but handy:
[Git for Windows](https://git-scm.com/downloads/win) (the offline installer
already includes Git),
[Windows Terminal](https://aka.ms/terminal) (tabs, better copy and paste),
[GitHub Desktop](https://desktop.github.com/) if you would rather not use git
commands.

### macOS

Open **Terminal** (press `Cmd` + `Space`, type *Terminal*, press Return).

1. **Apple's command line tools** (gives you `git` and a compiler):

   ```bash
   xcode-select --install
   ```

   A window pops up; click Install. If it says they are already installed,
   carry on. ([Apple's developer downloads](https://developer.apple.com/xcode/resources/))

2. **Homebrew**, the package manager. Copy the install command from the front
   page of **<https://brew.sh>** and paste it into Terminal. At the end it
   prints **"Next steps"** with two or three commands that add `brew` to your
   PATH. **Run them**; on Apple-silicon Macs (M1 and later) `brew` is not found
   until you do.

3. **The tools ESP-IDF needs:**

   ```bash
   brew install cmake ninja dfu-util ccache python3
   ```

4. **ESP-IDF v5.5.5 itself:**

   ```bash
   mkdir -p ~/esp
   cd ~/esp
   git clone -b v5.5.5 --recursive https://github.com/espressif/esp-idf.git
   cd ~/esp/esp-idf
   ./install.sh esp32s3
   ```

5. **Load it** into the current Terminal window. You do this **every time you
   open a new Terminal window** to work on the project:

   ```bash
   . $HOME/esp/esp-idf/export.sh
   ```

   (A dot, a space, then the path.) To make that shorter, add an alias once:

   ```bash
   echo 'alias get_idf=". $HOME/esp/esp-idf/export.sh"' >> ~/.zshrc
   ```

   then open a new window and type `get_idf`.

6. Check: `idf.py --version` should print `ESP-IDF v5.5.5`.

If anything says **"bad CPU type in executable"** on an Apple-silicon Mac,
install Rosetta 2 and try again:

```bash
/usr/sbin/softwareupdate --install-rosetta --agree-to-license
```

Official reference: [ESP-IDF v5.5.5 setup for Linux and macOS](https://docs.espressif.com/projects/esp-idf/en/v5.5.5/esp32s3/get-started/linux-macos-setup.html).

### Linux

1. **Packages.** Pick your distribution:

   Ubuntu, Debian, Linux Mint, Pop!_OS:

   ```bash
   sudo apt-get update
   sudo apt-get install git wget flex bison gperf python3 python3-pip python3-venv cmake ninja-build ccache libffi-dev libssl-dev dfu-util libusb-1.0-0
   ```

   Fedora, CentOS, RHEL:

   ```bash
   sudo dnf install git wget flex bison gperf python3 cmake ninja-build ccache dfu-util libusbx
   ```

   Arch, Manjaro:

   ```bash
   sudo pacman -S --needed gcc git make flex bison gperf python cmake ninja ccache dfu-util libusb python-pip
   ```

2. **ESP-IDF v5.5.5:**

   ```bash
   mkdir -p ~/esp
   cd ~/esp
   git clone -b v5.5.5 --recursive https://github.com/espressif/esp-idf.git
   cd ~/esp/esp-idf
   ./install.sh esp32s3
   ```

   (fish shell: `./install.fish esp32s3`)

3. **Permission to use USB serial ports.** Without this, flashing fails with
   *Permission denied*:

   ```bash
   sudo usermod -a -G dialout $USER     # Arch and Manjaro: use uucp instead of dialout
   ```

   **Log out and back in** (or reboot) for it to take effect.

4. **Load ESP-IDF** in every new terminal you use for the project:

   ```bash
   . $HOME/esp/esp-idf/export.sh
   ```

   Optional shortcut: `echo 'alias get_idf=". $HOME/esp/esp-idf/export.sh"' >> ~/.bashrc`

5. Check: `idf.py --version` prints `ESP-IDF v5.5.5`.

The project's scripts never hard-code a home folder: they find ESP-IDF through
`IDF_PATH`, which `export.sh` sets. If you installed ESP-IDF somewhere else,
point to that folder's `export.sh` instead.

### Any system: VS Code instead of a terminal

If you prefer buttons:

1. Install [Visual Studio Code](https://code.visualstudio.com/download).
2. Install the
   [ESP-IDF extension](https://marketplace.visualstudio.com/items?itemName=espressif.esp-idf-extension)
   (in VS Code: Extensions sidebar, search *ESP-IDF*, publisher *Espressif
   Systems*).
3. Open the command palette (`Ctrl` + `Shift` + `P`, or `Cmd` + `Shift` + `P`
   on a Mac), run **ESP-IDF: Configure ESP-IDF Extension**, choose **Express**,
   and select version **v5.5.5**. On macOS and Linux install the prerequisites
   from the sections above first; on Windows the extension fetches what it needs.
4. After [part 3](#3-get-the-code), use **File → Open Folder** on the project.
   The blue status bar at the bottom then has buttons for the target (choose
   **esp32s3**), the port, **Build**, **Flash** and **Monitor**.

[Extension documentation](https://docs.espressif.com/projects/vscode-esp-idf-extension/en/latest/).
The rest of this guide uses commands, because they are easier to copy exactly;
every command has a matching button in the extension.

---

## 3. Get the code

Choose a folder with **no spaces anywhere in its path**. The speech-recognition
library passes folder names to the linker without quotes, so a space breaks the
build at the very last step with an error like `cannot find Voice`.

| System | Good place |
| --- | --- |
| Windows | `C:\esp\esp-box-voice-assistant` (avoid the Desktop if OneDrive syncs it) |
| macOS, Linux | `~/esp/esp-box-voice-assistant` |

Windows (in the **ESP-IDF 5.5** window):

```bat
mkdir C:\esp
cd /d C:\esp
git clone https://github.com/Normansrule/esp-box-voice-assistant
cd esp-box-voice-assistant
```

macOS and Linux:

```bash
cd ~/esp
git clone https://github.com/Normansrule/esp-box-voice-assistant
cd esp-box-voice-assistant
```

**Planning to change the code and keep your changes on GitHub?** Fork first:
open the [repository page](https://github.com/Normansrule/esp-box-voice-assistant),
click **Fork**, and clone *your* copy
(`https://github.com/YOUR-NAME/esp-box-voice-assistant`) instead.

**Turn on the secret check once**, right after cloning. It stops you
committing an API key or Wi-Fi password by accident (more in
[part 9](#9-keep-your-secrets-out-of-git)):

```bash
git config core.hooksPath .githooks
```

---

## 4. Build

From the project folder, in your ESP-IDF terminal:

```bash
idf.py set-target esp32s3
idf.py build
```

- `set-target` is needed **once** per clone. It creates `sdkconfig` (your local
  build settings) from `sdkconfig.defaults`.
- The first `build` downloads the project's components and compiles everything:
  5 to 10 minutes. Later builds take seconds.
- Success ends with **`Project build complete.`** and a flash command
  suggestion.

See how much space is left:

```bash
idf.py size
```

---

## 5. Find your box's port (`ESP_PORT`)

The box appears as a serial port when it is plugged in and switched on. The
easiest way to find it is to list ports, plug the box in, and list again: the
new entry is the box.

| System | How to list ports | The box looks like |
| --- | --- | --- |
| Windows | **Device Manager** → *Ports (COM & LPT)*, or in PowerShell: `[System.IO.Ports.SerialPort]::GetPortNames()`, or in CMD: `mode` | `USB Serial Device (COM3)` → `COM3` |
| macOS | `ls /dev/cu.*` | `/dev/cu.usbmodem1101` |
| Linux | `ls /dev/serial/by-id/` (or `ls /dev/ttyACM*`) | `usb-Espressif_USB_JTAG_serial_debug_unit_…` → `/dev/ttyACM0` |

The number or name **can change** when you unplug, use another USB socket, or
put the box into download mode. If flashing suddenly cannot find the port, list
again.

Save it in a variable so the commands below stay the same. Use the line for
**your** terminal:

```bat
:: Windows, ESP-IDF 5.5 CMD
set ESP_PORT=COM3
```

```powershell
# Windows, ESP-IDF 5.5 PowerShell
$env:ESP_PORT = "COM3"
```

```bash
# macOS / Linux (bash or zsh)
export ESP_PORT=/dev/ttyACM0
```

On Linux, `/dev/serial/by-id/usb-Espressif…` is the most reliable name because
it does not change between plugs. Use the whole path.

---

## 6. Flash and watch it start

### 6a. Flash

```bat
:: Windows CMD
idf.py -p %ESP_PORT% flash monitor
```

```powershell
# Windows PowerShell
idf.py -p $env:ESP_PORT flash monitor
```

```bash
# macOS / Linux
idf.py -p "$ESP_PORT" flash monitor
```

The first flash writes the speech models and voice clips too, so it takes a
minute or two. Later flashes are quicker. When it finishes, the **monitor**
starts and shows the box's log. A good start ends with the self-test summary
and `Ready`.

**Leave the monitor** with `Ctrl` + `]`. (If your keyboard layout has no easy
`]`, use `Ctrl` + `T` then `Ctrl` + `X`.)

Only want to see the log, without flashing? `idf.py -p <port> monitor`.

### 6b. If it says "No serial data received" or cannot connect: download mode

Sometimes the box is too busy (or crashed) to accept new firmware. Put it into
**download mode** by hand:

1. Press and **hold BOOT**.
2. While holding it, **press and release RESET**.
3. **Release BOOT.**
4. Run the flash command again. (The port name may have changed; check
   [part 5](#5-find-your-boxs-port-esp_port).)

When flashing done this way finishes, the monitor may sit at
**`waiting for download`**. That is normal: the chip is still in download
mode. **Press RESET once** (do not hold BOOT) and the new firmware starts.
Do not press RESET while the progress percentage is still counting.

Unplugging the cable while holding BOOT and plugging it back in also enters
download mode.

### 6c. The web flasher (no install)

<https://normansrule.github.io/esp-box-voice-assistant/connect/>

- Works in **Chrome, Edge or Opera on a desktop computer** (they support Web
  Serial). Firefox and Safari do not.
- Plug in the box, press **Connect box**, and pick the port called
  *USB JTAG/serial debug unit*.
- **Published release** appears once a release has been published on GitHub.
  Until then, choose **Files from my own build** and select from your `build/`
  folder: `bootloader/bootloader.bin`, `partition_table/partition-table.bin`,
  `ota_data_initial.bin`, `esp_box_desk_buddy.bin`, `storage.bin`,
  `srmodels/srmodels.bin` and `flasher_args.json`.
- Status: tested in a browser without a box attached; not yet tested writing a
  real box. The command-line route above is the verified one.

---

## 7. First setup on the box

1. Tap the **gear** in the bottom-left corner.
2. Under **START HERE**, tap **Wi-Fi**, choose your network and type the
   password.

Wi-Fi is only used for the clock (and the optional online brain). The face,
timers, sensors, music and every offline phrase work without it. Say
**"Hi E.S.P."** to wake it, then try *"what can you do"*.

Your Wi-Fi name and password are saved **on the box** (in its NVS settings
area), never in the code. That is on purpose; see [part 9](#9-keep-your-secrets-out-of-git).

---

## 8. Change the code

### 8.1 The safe loop

Every change follows the same five steps:

1. **Make a branch** so `main` stays a known-good copy:
   `git switch -c my-change`
2. **Edit** the file (see the map below).
3. **Run the host tests** (seconds, no box needed):
   `python scripts/run_tests.py` (on macOS/Linux `python3` may be the name).
   They need a desktop C compiler: Xcode tools on macOS, `gcc` on Linux, and on
   Windows [MSYS2](https://www.msys2.org/) or [LLVM](https://github.com/llvm/llvm-project/releases).
   Without one the script says so and stops without pretending.
4. **Build and flash**: `idf.py build` then the flash command from part 6.
5. **Commit** when it works: `git add -A` then `git commit -m "What I changed"`.
   The secret check runs automatically.

If something breaks, `git switch main` gets you back to the last good version,
and `git diff` shows exactly what you changed.

### 8.2 Where everything lives

| Folder / file | What it is | Change it to... |
| --- | --- | --- |
| `main/main.c` | Start-up order | add a new module's init call |
| `main/buddy_config.h` | Every tunable number | blink rate, face size (`FACE_SCALE_PERCENT`), shake sensitivity |
| `main/face/` | Drawing the face | `face_emotions.c`: expressions and idle antics; `face_theme.c/.h`: colours |
| `main/voice/voice_commands.c` | Offline phrases it understands | add or reword phrases |
| `main/voice/voice_local_brain.c` | Offline replies, jokes, greetings | change what it says |
| `main/voice/voice_assistant.c` | The conversation flow | change what happens after the wake word |
| `main/voice/voice_router.c` | When to use offline, home or cloud AI | change the cost/privacy policy |
| `main/ui/` | Screens and settings menu | add a setting |
| `main/hardware/` | Display, speaker, microphones, buttons, IMU, battery, sensor dock | driver-level fixes |
| `main/net/` | Wi-Fi and network time | |
| `main/core/` | Settings storage, timers, serial console, self-test | |
| `main/Kconfig.projbuild` | Options shown in `idf.py menuconfig` | |
| `components/` | Board support from Espressif (patched local copy for the BOX-3 touch fix) | only if you know why |
| `spiffs/` | Sounds, music and voice clips flashed to the box | |
| `tools/`, `scripts/` | Data extraction, site build, clip rendering, tests | |
| `web/` | Simulator and web flasher site | |

The longer tour, with the reasoning behind each piece, is in
[FIRMWARE_GUIDE.md → How the code is organised](FIRMWARE_GUIDE.md#how-the-code-is-organised).

### 8.3 Theme, motion and other build options (`menuconfig`)

```bash
idf.py menuconfig
```

A text menu opens. **Arrow keys** move, **Enter** opens a menu, **Space**
toggles, **S** saves, **Q** quits. Look for **Desk Buddy appearance**:

- **Screen theme**: *Paper (day)* or *Ember (night, dark)*.
- **Reduce interface motion**: captions appear without sliding or fading.

Save, quit, then `idf.py build` and flash.

`menuconfig` writes to your local `sdkconfig`, which is **not** committed (it
is per-machine). To make a choice the default for everyone, add the line to
`sdkconfig.defaults` by hand (for example `CONFIG_BUDDY_THEME_EMBER=y`), then
delete `sdkconfig` and run `idf.py fullclean` and `idf.py build` so the new
default is picked up. Avoid `idf.py save-defconfig`: it rewrites
`sdkconfig.defaults` and throws away its explanatory comments.

### 8.4 Faces and expressions

- **Add an expression:** add a name to `face_emotion_t` in
  `main/face/face_emotions.h`, a row of numbers to `s_emotion_poses` in
  `main/face/face_emotions.c` (the comment above the table explains each
  number), and its lower-case name to `s_emotion_names`. No drawing code
  changes. Full walk-through: [FIRMWARE_GUIDE.md](FIRMWARE_GUIDE.md#adding-an-expression-start-to-finish).
- **Try it without saying anything:** in the monitor, type `faces` to list
  them and `face happy` (or any name) to show one.
- **Colours:** `main/face/face_theme.c`. The rules for keeping text readable
  are in [DESIGN.md](DESIGN.md).

### 8.5 Voice phrases and replies

**Add a phrase** in `main/voice/voice_commands.c`: one row per wording, for
example

```c
EN(VOICE_ACTION_TELL_JOKE,         "tell me another joke"),
```

If it needs a brand-new action, add it to `voice_action_t` in
`voice_commands.h` and handle it in `voice_local_brain.c`.

Rules that matter:

- **Keep the English list at about 150 phrases or fewer** (it is about 147
  now). Every phrase uses some of the chip's scarce internal RAM. After
  flashing, check the boot log: the self-test's *Internal RAM* line must stay
  `ok`. If it drops, remove phrases rather than taking memory elsewhere.
- Three to five syllables per phrase works best; avoid phrases that rhyme with
  each other; give each action two or three wordings.
- Test without speaking: in the monitor, `phrases` lists them and
  `say what time is it` runs one.

**Change a reply** in the tables near the top of `main/voice/voice_local_brain.c`.
Reply text costs no RAM. Each fixed reply has a matching spoken clip in
`spiffs/voice/`, named by a hash of the text; reword a reply and it simply goes
quiet (caption only) until you render clips again:

```bash
python scripts/make_voice_clips.py
```

That needs [eSpeak NG](https://github.com/espeak-ng/espeak-ng/releases) and
[FFmpeg](https://ffmpeg.org/download.html) installed (Linux:
`sudo apt-get install espeak-ng ffmpeg`; macOS: `brew install espeak-ng ffmpeg`).
The `storage` area on the box is nearly full, so long new sentences may not fit;
the build fails with `SpiffsFullError` if they do not.

**After any change to faces, phrases or replies**, regenerate the website data
or CI will fail:

```bash
python tools/extract_firmware_data.py
python tools/render_readme_art.py
```

and commit the files they change.

### 8.6 Other useful monitor commands

Type these into the monitor and press Enter: `help`, `status`, `selftest`,
`volume 50`, `brightness 80`, `timer 5`, `timer cancel`, `motion`,
`motion stream`, `battery`, `reboot`.

---

## 9. Keep your secrets out of git

A public GitHub repository is read by bots within minutes. Anything committed
there, even for one second, should be treated as leaked: deleting it in the next
commit does not remove it from history.

**What counts as a secret here:** your Wi-Fi password, an OpenAI (or any) API
key, GitHub tokens, private keys, and recordings of your voice.

**How this repository protects you:**

| Protection | What it does |
| --- | --- |
| Wi-Fi set on the touchscreen | Your network name and password never appear in source code. Do not add them to code "just for testing". |
| `.gitignore` | Keeps `sdkconfig`, `build/`, `spiffs/openai_key.txt`, `.env`, `*.pem`, `*.key`, firmware images and signing keys out of `git add`. |
| `.githooks/pre-commit` | Runs `scripts/check_secrets.py --staged` before every commit and blocks keys, tokens, Wi-Fi passwords and forbidden files. Turn it on with `git config core.hooksPath .githooks` (once per clone). |
| CI *No secrets committed* job | Runs the same check on every push and pull request, and the release workflow refuses to build if it fails. |
| GitHub secret scanning and push protection | GitHub's own scanner; see [GITHUB_SECURITY.md](GITHUB_SECURITY.md) for switching it on. |

**Rules of thumb:**

- The optional API key goes in `spiffs/openai_key.txt` (copy
  `spiffs/openai_key.example.txt`). It is git-ignored, but it **is** baked into
  `build/storage.bin` when you build. **Never share your `build/` folder or
  firmware images you built yourself**, and never post a flash dump
  (`esptool read_flash`): the box's settings area contains your Wi-Fi password
  and key in plain form.
- Use a project-scoped API key with a low monthly spending limit.
- If the hook blocks a commit, read the message: it names the file and line.
  Remove the secret, or for a harmless fake example add the comment
  `secret-scan: allow` to that line.
- **If a real key was ever pushed: revoke it and make a new one** on the
  provider's website first, then clean up. That is the only real fix.
- Run the check yourself any time: `python scripts/check_secrets.py`.
- Do not paste boot logs publicly without reading them; they are designed not
  to print keys or passwords, but check.

Flash encryption and secure boot are **not** enabled by this project yet (see
[SECURITY.md](../SECURITY.md)). Anyone with your box and a USB cable can read
what is on it.

---

## 10. Put your changes on GitHub

**First time only, make your account safe:** turn on
[two-factor authentication](https://docs.github.com/en/authentication/securing-your-account-with-two-factor-authentication-2fa/configuring-two-factor-authentication).
Repository settings worth switching on are listed in
[GITHUB_SECURITY.md](GITHUB_SECURITY.md).

**Signing in from the terminal:** GitHub no longer accepts your account
password for `git push`. Either:

- install the [GitHub CLI](https://cli.github.com/) and run `gh auth login`
  (simplest), or
- use [GitHub Desktop](https://desktop.github.com/), which handles sign-in for
  you, or
- set up an [SSH key](https://docs.github.com/en/authentication/connecting-to-github-with-ssh)
  with a passphrase.

Never paste a personal access token into a file in the project.

**The usual flow:**

```bash
git switch -c my-change                 # once per change
git add -A
git commit -m "Add a 'tell me another joke' phrase"
git push -u origin my-change
```

Then open the link `git push` prints, or the repository page, and create a
**pull request**. CI builds the firmware, runs the tests and the secret check;
merge only when it is green.

**Keeping your fork up to date:** on your fork's GitHub page click
**Sync fork**, then `git switch main` and `git pull`.

---

## 11. Recover the box, or go back to the factory firmware

Nothing in this guide can permanently damage the box. The ROM bootloader that
download mode uses cannot be overwritten.

**The box crashes, boot-loops or shows a blank screen:**

1. Enter download mode ([6b](#6b-if-it-says-no-serial-data-received-or-cannot-connect-download-mode)).
2. Erase everything (this also forgets Wi-Fi and the saved settings):

   ```bash
   idf.py -p <port> erase-flash
   ```

3. Flash again with `idf.py -p <port> flash monitor`, then press RESET if it
   says `waiting for download`.

**Going back to Espressif's factory demo:**

- Easiest: open the [ESP Launchpad](https://espressif.github.io/esp-launchpad/)
  in Chrome or Edge, connect, choose the **ESP32-S3-BOX-3** factory firmware
  and flash.
- Or download the factory image from the
  [espressif/esp-box](https://github.com/espressif/esp-box) repository
  (its README links the current file, `factory_test_fw-ESP-BOX-3.bin` at the
  time of writing) and write it at address 0:

  ```bash
  python -m esptool --chip esp32s3 -p <port> write_flash 0x0 factory_test_fw-ESP-BOX-3.bin
  ```

  (Run it from an ESP-IDF terminal, which already has `esptool`.)
- The [web flasher](https://normansrule.github.io/esp-box-voice-assistant/connect/)
  can write a single merged image at offset 0x0 too.

To come back to this project afterwards, just flash it again (part 6).

---

## 12. Troubleshooting: every error we know about

Find the message you see. Each entry says what it means and what to do.

### Installing and terminals

<a id="idfpy-not-found"></a>
**`'idf.py' is not recognized…` / `idf.py: command not found`**
You are in a normal terminal. On Windows, open the **ESP-IDF 5.5 CMD** or
**PowerShell** shortcut. On macOS/Linux run `. $HOME/esp/esp-idf/export.sh`
(or `get_idf`) in this window. This has to be done in every new window.

<a id="idfpy-in-wsl-or-git-bash"></a>
**Running `idf.py` in WSL or Git Bash on Windows**
WSL is a separate Linux with no access to the box's USB port unless you add
[usbipd-win](https://learn.microsoft.com/en-us/windows/wsl/connect-usb), and it
needs its own ESP-IDF. Git Bash is not set up by the installer. Close them and
use the ESP-IDF 5.5 shortcut.

**`No such file or directory` after pasting a Windows path in bash**
Bash treats `\` as an escape character, so `cd C:\Users\me\Desktop\project`
loses all its backslashes. In Git Bash write `/c/Users/me/Desktop/project`; in
WSL write `/mnt/c/Users/me/Desktop/project`; or put the path in single quotes.
In CMD and PowerShell backslashes are fine. On Windows CMD, use `cd /d` to
change drive as well as folder.

**`ESP-IDF Python virtual environment … not found` / Python or pip errors**
The tools install is incomplete or was done for a different ESP-IDF folder.
Windows: run the installer again and pick the same ESP-IDF folder. macOS/Linux:
`cd ~/esp/esp-idf && ./install.sh esp32s3`, then `. ./export.sh` again.

**`idf.py --version` shows v5.4, v6.0 or something other than v5.5.x**
You have several ESP-IDF installs. Use the v5.5 shortcut (Windows), or the
`export.sh` inside your v5.5.5 folder. Then delete `sdkconfig` and `build/` in
the project and rebuild.

**macOS: `bad CPU type in executable`** → install Rosetta 2 (command in the
[macOS section](#macos)).
**macOS: `xcrun: error: invalid active developer path`** → `xcode-select --install`
(this happens after big macOS updates).
**macOS: `brew: command not found`** → run the "Next steps" commands Homebrew
printed, or open a new Terminal window.

**Windows: very slow builds, or `Access is denied` on files under `build/`**
OneDrive or an antivirus is scanning the build folder. Move the project out of
a OneDrive-synced Desktop or Documents folder (for example to `C:\esp\`), and
consider excluding that folder from real-time scanning.

**Windows: path too long / non-English characters in the user name**
ESP-IDF limits paths to 90 characters and dislikes non-ASCII characters. Put
both ESP-IDF and the project under `C:\esp\` or `C:\Espressif\`.

### Building

**`cannot find Voice` (or any half of a folder name) at the very end of a build**
There is a space in the project path. Move or rename the folder so the path
has no spaces, then `idf.py fullclean` and build again.

**`driver/i2c.h` or other I2C errors, or many errors in `components/`**
You are building with ESP-IDF v6. This project needs v5.5.x.

**A change in `sdkconfig.defaults` does nothing**
Defaults only apply when there is no `sdkconfig`. Delete `sdkconfig`, run
`idf.py fullclean`, then `idf.py build`.

**Downloading components fails, or `Failed to resolve component`**
The first build fetches components from the
[Espressif Component Registry](https://components.espressif.com/). Check your
internet connection or proxy, then build again. If it stays stuck, delete the
`managed_components` folder and build again.

**`SpiffsFullError` / `storage` image too big**
The voice clips and music no longer fit in the storage area. Remove the clips
or music you added, or shorten new replies.

**Host tests: `no desktop C compiler found`**
Install Xcode tools (macOS), `gcc` (Linux), or MSYS2/LLVM (Windows). The
firmware itself is fine; only the host tests need this.

**CI fails at *Simulator data and README art are fresh***
Run `python tools/extract_firmware_data.py` and
`python tools/render_readme_art.py`, commit what changed, push again.

### Connecting and flashing

**`Failed to connect to ESP32-S3: No serial data received`**
1. Enter download mode by hand ([6b](#6b-if-it-says-no-serial-data-received-or-cannot-connect-download-mode)) and try again.
2. Close anything else using the port: another monitor, a browser tab with the
   web flasher, Arduino IDE, Cura, serial terminals.
3. Try another **data** cable and a USB port directly on the computer (not a
   hub or a monitor's USB port).
4. List ports again; the port often changes in download mode.

**Monitor stuck at `waiting for download`**
The chip is in download mode. Press **RESET** once.

**`Could not open port … Permission denied` (Linux)**
Add yourself to `dialout` (`uucp` on Arch) and log out and in again
([Linux step 3](#linux)). If it still fails right after plugging in on Ubuntu,
ModemManager may be probing the port: wait ten seconds, or run
`sudo systemctl stop ModemManager`.

**`could not open port 'COM3': PermissionError(13, 'Access is denied.')` (Windows)**
or **`Resource busy` (macOS)**: another program has the port open. Close other
monitors, browser tabs and serial tools, unplug and replug.

**`The port doesn't exist` / no new port appears when you plug in**
Charge-only cable, box switched off, or a bad USB socket. Try another cable and
port. Then hold **BOOT** while plugging in.

**Flash succeeds but the screen stays dark / the box restarts in a loop**
Read the monitor: the last lines before the restart say why. Copy them into an
issue. Then do the [erase and reflash](#11-recover-the-box-or-go-back-to-the-factory-firmware).

**`Ctrl` + `]` does not quit the monitor**
Use `Ctrl` + `T` then `Ctrl` + `X`, or just close the window.

**Web flasher: no *Connect* dialog, or "Web Serial not supported"**
Use Chrome, Edge or Opera on a desktop computer. Firefox, Safari and phones do
not support Web Serial. On Linux the `dialout` rule above applies to the
browser too.

### On the box

**It never connects to Wi-Fi / says *Connecting* forever**
The box only supports 2.4 GHz networks. Check the password; see the longer
[Wi-Fi notes](FIRMWARE_GUIDE.md#it-says-connecting-and-never-connects).

**It does not react to "Hi E.S.P."**
Check the mute switch, speak from about a metre away, and see
[FIRMWARE_GUIDE.md → Hearing you in a noisy room](FIRMWARE_GUIDE.md#hearing-you-in-a-noisy-room).

**Self-test says Internal RAM is low after adding phrases**
Remove some phrases (see [8.5](#85-voice-phrases-and-replies)).

### Git and GitHub

**`git push` asks for a password and then fails**
GitHub does not accept account passwords. Use `gh auth login`, GitHub Desktop
or an SSH key ([part 10](#10-put-your-changes-on-github)).

**The commit is blocked by `check_secrets`**
Working as intended. Read which file and line it named; see
[part 9](#9-keep-your-secrets-out-of-git).

**GitHub says *push declined due to repository rule violations* or
*secret detected***
GitHub's push protection found a secret. Remove it from the commit
(`git reset --soft HEAD~1`, fix, commit again) and **rotate the key**.

**Still stuck?** Open an
[issue](https://github.com/Normansrule/esp-box-voice-assistant/issues) with
your system, the ESP-IDF version (`idf.py --version`), the exact command and
the last 30 lines of output. Remove Wi-Fi names, passwords and keys before
posting.

---

## 13. Accessibility and other setups

- **Screen readers.** Everything here works from a terminal, which screen
  readers handle well: Windows Terminal with NVDA or Narrator, macOS Terminal
  with VoiceOver, GNOME Terminal with Orca. Commands are listed in code blocks
  so they can be copied rather than read out character by character. The VS
  Code extension is also keyboard- and screen-reader-accessible through the
  command palette.
- **No mouse or limited dexterity.** `menuconfig` is keyboard-only by design.
  Most box features can be driven from the monitor (`face`, `say`, `volume`,
  `timer`…) instead of the touchscreen.
- **Motion sensitivity.** Turn on **Reduce interface motion** in `menuconfig`
  ([8.3](#83-theme-motion-and-other-build-options-menuconfig)).
- **Low vision or night use.** The **Ember** theme is dark with a bright face;
  `brightness` and `volume` can be set by voice or from the monitor. Colour
  contrast rules are in [DESIGN.md](DESIGN.md).
- **Deaf or hard of hearing.** Every spoken reply is also shown as a caption.
- **Older or slower computers.** Everything works, the first build just takes
  longer. Close other programs; later builds are quick.
- **Chromebook.** The web flasher should work in Chrome on ChromeOS (untested).
  Building needs the Linux development environment with USB access to the box;
  follow the Linux steps (untested).
- **Not comfortable in English.** Espressif's ESP-IDF documentation is also
  available in Chinese (switch language at the top of their docs). The box
  understands Chinese commands too; see `voice_commands.c`.
- **Corporate or school laptops.** You may not be allowed to install drivers or
  software. The web flasher needs no install; otherwise ask your IT team for
  ESP-IDF and VS Code.

---

## Glossary

| Word | Meaning |
| --- | --- |
| ESP-IDF | Espressif's free toolkit for programming ESP32 chips |
| `idf.py` | The ESP-IDF command that builds, flashes and monitors |
| Flash (verb) | Copy the firmware onto the box |
| Firmware | The program that runs on the box |
| Port / `ESP_PORT` | The name your computer gives the box's USB connection (`COM3`, `/dev/ttyACM0`…) |
| Monitor | The live log the box prints over USB; also where you type console commands |
| Download mode | A special start-up mode (BOOT + RESET) where the box only waits for new firmware |
| `sdkconfig` | Your local build settings; generated, never committed |
| `menuconfig` | The text menu that edits `sdkconfig` |
| SPIFFS / storage | The area of flash that holds sounds, music and voice clips |
| NVS | The area of flash that holds saved settings such as Wi-Fi |
| Wake word | "Hi E.S.P.", the phrase that starts listening |
| CI | GitHub's automatic build and test on every push |
| Fork | Your own copy of the repository on GitHub |
| Branch | A separate line of changes inside a repository |
