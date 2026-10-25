# Home AI: the buddy's own brain, on your computer

Status: **VERIFIED IN TEST** (server wire format, router policy) and **live on a
Windows PC end to end**: a spoken 16 kHz WAV sent to the LAN address the way the
firmware sends it was heard, answered and spoken back as MP3 in about 1.6 s
(hear 0.5 s on CPU, think 0.6 s on GPU, speak 0.5 s). **UNTESTED ON DEVICE**: the
box itself talking to this server over Wi-Fi has not been tried yet.

Whisper runs on the CPU unless the CUDA 12 cuBLAS/cuDNN libraries are present;
the server tries the GPU, checks it with one transcription, and falls back. On
the CPU, base.en still takes about half a second per question.

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

## What it can answer, and how sure we are

Labels: **VERIFIED IN TEST** = automated tests pass; **LIVE ON PC** = tried for
real with `--ask` on Alek's PC (RTX 5060 Ti, llama3.2 3B); **UNTESTED** = written,
not yet run for real. Nothing here is verified on the box itself yet.

| Ability | How | Status |
| --- | --- | --- |
| General questions ("why do cats purr", "who wrote Romeo and Juliet") | llama3.2 | LIVE ON PC |
| Sums and percentages ("17% of 2350", "25% off 40 dollars") | calculator tool | VERIFIED IN TEST, LIVE ON PC |
| Word problems ("3 boxes of 12 eggs, 5 break") | model sets it up, calculator does the maths | VERIFIED IN TEST, LIVE ON PC |
| Dates ("days until Christmas", "what day is 14 March 2027") | date tool + this PC's clock | VERIFIED IN TEST, LIVE ON PC |
| Clock maths ("trip of 95 min from 2:40 pm", "2 h before 6:50") | time tool | VERIFIED IN TEST, LIVE ON PC |
| Unit conversion (length, weight, volume, speed, time, temperature) | unit tool | VERIFIED IN TEST, LIVE ON PC |
| Follow-up questions ("and in Fahrenheit?") | the box keeps the last 8 exchanges | UNTESTED |
| Step-by-step reasoning (`--think`) | a reasoning model thinks first; only the answer reaches the box | VERIFIED IN TEST (scripted); UNTESTED live, needs `ollama pull qwen3:4b` |

**The tool result always wins.** In live testing llama3.2 often called the
calculator correctly and then said a different number ("27 eggs" when the tool
said 31). So the home brain checks: if the answer does not contain the tool's
number, it says the tool's result instead. Of 12 test questions on the PC, 3
were fixed this way and all 12 ended up right; without the check 3 were wrong.

What it still cannot do: anything needing the internet (weather, news, live
scores), and anything the model simply does not know, where it may be wrong
with confidence. A bigger model (`--model qwen3:8b`) knows more and reasons
better, at a few seconds per answer.

### Try it without the box

```bash
python tools/home_brain/home_brain.py --ask "If a shirt is 40 dollars and 25 percent off, what do I pay?"
```

This prints any tool calls and the exact answer the box would say. It needs
only Ollama.

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
| `--think` | off | let a reasoning model (qwen3, deepseek-r1) think first |
| `--no-tools` | off | plain chat, no calculator, date or unit tools |
| `--ask "..."` | | answer one typed question and exit |

## Privacy and safety

- Audio is handled in memory and never saved.
- It listens on your local network over plain HTTP, with no password. Run it
  only on a network you trust, and do not forward its port to the internet.
- The box sends the same personality prompt it sends OpenAI; nothing else.

## Tests

```bash
python tools/home_brain/test_home_brain.py
python tools/home_brain/test_brain_tools.py
```

The first checks the exact JSON and multipart format the firmware's client
expects; the second checks the tools, the answer tidying, and the tool loop
against a scripted model that fumbles numbers the way llama3.2 did. Both use
stand-ins for the models, so they need nothing installed.
