# Home AI: the buddy's own brain, on your computer

Status: **VERIFIED IN TEST** (server wire format, router policy) and **live on a
Windows PC** for the answering and voice steps. **UNTESTED ON DEVICE**: the box
talking to this server over Wi-Fi has not been tried on hardware yet.

## What it is

Pick **Home AI** on the box and questions it does not recognise go to a small
open AI running on a computer on your Wi-Fi, not to OpenAI:

| Step | What does it | Where |
| --- | --- | --- |
| Hear | Whisper (`faster-whisper`, `base.en`) | this computer |
| Think | `llama3.2` (3B) through Ollama | this computer |
| Speak | Windows' built-in voice, espeak-ng, or Piper, as MP3 | this computer |

No account, no API key, no bill, no internet once the models are downloaded.
The box's hundred-odd offline phrases still work with the computer switched
off; when it is off, the box notices in about a second and answers what it can
by itself.

### Why not on the chip itself

An ESP32-S3 can run a language model of a few million parameters, a few words a
second, and a model that small cannot answer general questions. The chip also
cannot turn free speech into text on its own (its recogniser only knows the
phrases registered in advance). A 3B model on a PC answers properly, and on a
GPU in well under a second.

## Setup

1. Install [Ollama](https://ollama.com), then: `ollama pull llama3.2`
2. Install ffmpeg (Windows: `winget install Gyan.FFmpeg`; Ubuntu: `sudo apt install ffmpeg espeak-ng`).
3. From the repository root:

```bash
python -m pip install -r tools/home_brain/requirements.txt
python tools/home_brain/home_brain.py
```

It prints an address like `192.168.1.50:8765`. Windows will ask whether to let
Python through the firewall: allow it on **private** networks only.

4. On the box: **gear → When to ask → Home AI**, then **Home AI address** and type
   the address it printed.

### Options

| Flag | Default | |
| --- | --- | --- |
| `--model` | `llama3.2` | any model you have pulled in Ollama, e.g. `qwen2.5:3b` |
| `--whisper-model` | `base.en` | `small.en` hears better and is slower |
| `--device` | `auto` | `cuda` or `cpu` for Whisper |
| `--piper-voice` | none | path to a Piper `.onnx` voice (needs `piper-tts`) |
| `--port` | `8765` | |
| `--quiet` | off | stop printing what it heard and answered |

## Privacy and safety

- Audio is handled in memory and never saved.
- It listens on your local network over plain HTTP, with no password. Run it
  only on a network you trust, and do not forward its port to the internet.
- The box sends the same personality prompt it sends OpenAI; nothing else.

## Tests

```bash
python tools/home_brain/test_home_brain.py
```

These check the exact JSON and multipart format the firmware's client expects,
with stand-ins for the models, so they need nothing installed.
