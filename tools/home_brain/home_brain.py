#!/usr/bin/env python3
"""
home_brain.py — The Desk Buddy's own AI, running on a computer on your Wi-Fi.

SPDX-License-Identifier: Unlicense OR CC0-1.0

WHAT IT IS
----------
A small server that answers the box's questions without OpenAI, an account, a
key or a bill. The box records a question and sends it here; this program

    1. HEARS it    with Whisper (faster-whisper), on this computer
    2. ANSWERS it  with a small open model through Ollama (llama3.2 by default)
    3. SAYS it     with Piper, or the voice built into Windows, as MP3

and sends the three results back. It speaks the same OpenAI-shaped API the
firmware already uses for its online brain, so on the box it is only a
different address — pick "Home AI" under Settings > When to ask, and type the
address this program prints when it starts.

WHY NOT ON THE CHIP
-------------------
An ESP32-S3 can run a language model of a few million parameters, slowly, and
one that small cannot answer general questions. It also cannot turn free
speech into text by itself. A 3-billion-parameter model on an ordinary PC can,
in a second or two, and that is the honest way to give the box an AI of its
own. The box's offline phrases keep working with this computer switched off.

PRIVACY
-------
Audio is decoded in memory and never written to disk or kept. Only the box on
your own network talks to this server; nothing here reaches the internet once
the models are downloaded. Questions and answers are printed to the console
so you can see what it heard; pass --quiet to stop that.

RUNNING IT
----------
    ollama pull llama3.2
    python -m pip install -r tools/home_brain/requirements.txt
    python tools/home_brain/home_brain.py

See tools/home_brain/README.md for the details, including the firewall prompt.
"""

import argparse
import io
import json
import os
import shutil
import socket
import subprocess
import sys
import tempfile
import threading
import time
import urllib.error
import urllib.request
import wave
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

DEFAULT_PORT = 8765
DEFAULT_OLLAMA = "http://127.0.0.1:11434"
DEFAULT_MODEL = "llama3.2"
DEFAULT_WHISPER = "base.en"

# Largest request body accepted. The box records at most ten seconds of 16 kHz
# mono audio, about 320 KB; this leaves room and still refuses anything silly.
MAX_BODY_BYTES = 4 * 1024 * 1024


# --------------------------------------------------------------------------- #
# Hearing: speech to text                                                      #
# --------------------------------------------------------------------------- #

class WhisperEars:
    """faster-whisper, loaded once. GPU when it works, CPU when it does not."""

    def __init__(self, model_name, device):
        from faster_whisper import WhisperModel  # Imported here: optional.

        self.model = None
        attempts = [("cuda", "float16"), ("cpu", "int8")] if device == "auto" else \
                   [(device, "float16" if device == "cuda" else "int8")]
        last_error = None
        for dev, compute in attempts:
            try:
                self.model = WhisperModel(model_name, device=dev, compute_type=compute)
                self.description = "Whisper %s on %s" % (model_name, dev)
                return
            except Exception as error:  # A missing CUDA library is the usual one.
                last_error = error
        raise RuntimeError("could not load Whisper: %s" % last_error)

    def transcribe(self, audio_bytes, language):
        segments, _ = self.model.transcribe(io.BytesIO(audio_bytes),
                                            language=language or None,
                                            beam_size=1, vad_filter=True)
        return " ".join(segment.text.strip() for segment in segments).strip()


# --------------------------------------------------------------------------- #
# Thinking: the language model                                                 #
# --------------------------------------------------------------------------- #

class OllamaMind:
    """A model served by Ollama, asked through its /api/chat endpoint."""

    def __init__(self, base_url, model, max_tokens):
        self.base_url = base_url.rstrip("/")
        self.model = model
        self.max_tokens = max_tokens
        self.description = "%s via Ollama" % model

    def check(self):
        """Raise with a useful message when Ollama or the model is missing."""
        try:
            with urllib.request.urlopen(self.base_url + "/api/tags", timeout=3) as reply:
                tags = json.load(reply)
        except (urllib.error.URLError, OSError) as error:
            raise RuntimeError("Ollama is not answering at %s (%s). Start it, or install it "
                               "from https://ollama.com" % (self.base_url, error))
        names = {m.get("name", "") for m in tags.get("models", [])}
        if self.model not in names and (self.model + ":latest") not in names:
            raise RuntimeError("Ollama does not have %s yet. Run: ollama pull %s"
                               % (self.model, self.model))

    def reply(self, messages, max_tokens, temperature):
        limit = min(int(max_tokens or self.max_tokens), self.max_tokens)
        body = {
            "model": self.model,
            "messages": messages,
            "stream": False,
            # Keep the model loaded between questions, so the second one is fast.
            "keep_alive": "30m",
            "options": {"num_predict": limit,
                        "temperature": 0.7 if temperature is None else float(temperature)},
        }
        request = urllib.request.Request(self.base_url + "/api/chat",
                                         data=json.dumps(body).encode("utf-8"),
                                         headers={"Content-Type": "application/json"})
        with urllib.request.urlopen(request, timeout=60) as reply:
            answer = json.load(reply)
        text = answer.get("message", {}).get("content", "").strip()
        used = int(answer.get("prompt_eval_count", 0)) + int(answer.get("eval_count", 0))
        return text, used


# --------------------------------------------------------------------------- #
# Speaking: text to MP3                                                        #
# --------------------------------------------------------------------------- #

class Voice:
    """
    Piper if a voice model was given, otherwise the computer's own voice:
    Windows' built-in speech, or espeak-ng on Linux. The box plays MP3, so the
    result goes through ffmpeg (or lameenc) on the way out.
    """

    def __init__(self, piper_model=None):
        self.piper = None
        if piper_model:
            from piper import PiperVoice  # Imported here: optional.
            self.piper = PiperVoice.load(piper_model)
            self.description = "Piper (%s)" % os.path.basename(piper_model)
        elif os.name == "nt":
            self.description = "Windows built-in voice"
        elif shutil.which("espeak-ng"):
            self.description = "espeak-ng"
        else:
            raise RuntimeError("no voice available: pass --piper-voice, or install espeak-ng")

        self.ffmpeg = shutil.which("ffmpeg")
        if self.ffmpeg is None:
            try:
                import lameenc  # noqa: F401
            except ImportError:
                raise RuntimeError("no MP3 encoder: install ffmpeg, or pip install lameenc")

    def _wav(self, text):
        if self.piper is not None:
            buffer = io.BytesIO()
            with wave.open(buffer, "wb") as wav_file:
                if hasattr(self.piper, "synthesize_wav"):
                    self.piper.synthesize_wav(text, wav_file)
                else:
                    self.piper.synthesize(text, wav_file)
            return buffer.getvalue()

        handle, path = tempfile.mkstemp(suffix=".wav")
        os.close(handle)
        try:
            if os.name == "nt":
                # The text goes in through the environment, never the command
                # line, so nothing in it can be read as PowerShell.
                script = ("Add-Type -AssemblyName System.Speech;"
                          "$s = New-Object System.Speech.Synthesis.SpeechSynthesizer;"
                          "$s.SetOutputToWaveFile($env:HOME_BRAIN_WAV);"
                          "$s.Speak($env:HOME_BRAIN_TEXT); $s.Dispose()")
                env = dict(os.environ, HOME_BRAIN_WAV=path, HOME_BRAIN_TEXT=text)
                subprocess.run(["powershell", "-NoProfile", "-NonInteractive", "-Command", script],
                               env=env, check=True, capture_output=True, timeout=30)
            else:
                subprocess.run(["espeak-ng", "-w", path, "--", text],
                               check=True, capture_output=True, timeout=30)
            with open(path, "rb") as wav_file:
                return wav_file.read()
        finally:
            os.remove(path)

    def mp3(self, text):
        wav_bytes = self._wav(text)
        if self.ffmpeg:
            result = subprocess.run([self.ffmpeg, "-hide_banner", "-loglevel", "error",
                                     "-i", "pipe:0", "-ac", "1", "-ar", "24000",
                                     "-b:a", "64k", "-f", "mp3", "pipe:1"],
                                    input=wav_bytes, capture_output=True, check=True, timeout=30)
            return result.stdout

        import lameenc
        with wave.open(io.BytesIO(wav_bytes), "rb") as wav_file:
            frames = wav_file.readframes(wav_file.getnframes())
            encoder = lameenc.Encoder()
            encoder.set_bit_rate(64)
            encoder.set_in_sample_rate(wav_file.getframerate())
            encoder.set_channels(wav_file.getnchannels())
            encoder.set_quality(5)
        return encoder.encode(frames) + encoder.flush()


# --------------------------------------------------------------------------- #
# The OpenAI-shaped wire format                                                #
# --------------------------------------------------------------------------- #

def parse_multipart(body, content_type):
    """
    Split a multipart/form-data body into {name: bytes}.

    Small and strict on purpose: the only client is the box's firmware, which
    always sends the same handful of fields. (Python's cgi module, which used
    to do this, was removed in 3.13.)
    """
    marker = "boundary="
    if marker not in content_type:
        raise ValueError("multipart body without a boundary")
    boundary = content_type.split(marker, 1)[1].split(";")[0].strip().strip('"')
    delimiter = b"--" + boundary.encode("latin-1")

    fields = {}
    for part in body.split(delimiter):
        if part in (b"", b"--", b"--\r\n") or part.startswith(b"--"):
            continue
        part = part[2:] if part.startswith(b"\r\n") else part
        head, _, content = part.partition(b"\r\n\r\n")
        if content.endswith(b"\r\n"):
            content = content[:-2]
        name = None
        for line in head.decode("latin-1").split("\r\n"):
            if line.lower().startswith("content-disposition:") and 'name="' in line:
                name = line.split('name="', 1)[1].split('"', 1)[0]
        if name:
            fields[name] = content
    return fields


def error_body(message, code="home_brain_error"):
    """The error shape the firmware recognises, so it is never read aloud."""
    return {"error": {"message": message, "type": code, "code": code}}


def chat_completion_body(text, model, tokens_used):
    """A chat completion reply with the fields the firmware's parser requires."""
    return {
        "id": "home-%d" % int(time.time() * 1000),
        "object": "chat.completion",
        "created": int(time.time()),
        "model": model,
        "choices": [{"index": 0,
                     "message": {"role": "assistant", "content": text},
                     "finish_reason": "stop"}],
        # Required: the firmware's OpenAI client refuses a reply without it.
        "usage": {"prompt_tokens": 0, "completion_tokens": tokens_used,
                  "total_tokens": tokens_used},
    }


class HomeBrain:
    """The three abilities together. Any of them may be None if unavailable."""

    def __init__(self, ears, mind, voice, quiet=False):
        self.ears = ears
        self.mind = mind
        self.voice = voice
        self.quiet = quiet
        # One question at a time: a GPU shared by three models does not get
        # faster by being asked twice at once, and the box only asks one.
        self.lock = threading.Lock()

    def say(self, *parts):
        if not self.quiet:
            print(*parts, flush=True)


def make_handler(brain):
    class Handler(BaseHTTPRequestHandler):
        server_version = "DeskBuddyHomeBrain/1"

        def log_message(self, fmt, *args):  # The default logs every request.
            pass

        def _path(self):
            path = self.path.split("?", 1)[0].rstrip("/")
            return path[3:] if path.startswith("/v1") else path

        def _send(self, status, payload=None, content_type="application/json", raw=None):
            data = raw if raw is not None else json.dumps(payload).encode("utf-8")
            self.send_response(status)
            self.send_header("Content-Type", content_type)
            self.send_header("Content-Length", str(len(data)))
            self.end_headers()
            self.wfile.write(data)

        def _body(self):
            length = int(self.headers.get("Content-Length") or 0)
            if length <= 0 or length > MAX_BODY_BYTES:
                raise ValueError("request body missing or too large")
            return self.rfile.read(length)

        def do_GET(self):
            if self._path() in ("/health", ""):
                self._send(200, {"ok": True,
                                 "hears": brain.ears.description if brain.ears else None,
                                 "thinks": brain.mind.description if brain.mind else None,
                                 "speaks": brain.voice.description if brain.voice else None})
            else:
                self._send(404, error_body("no such endpoint", "not_found"))

        def do_POST(self):
            path = self._path()
            try:
                body = self._body()
                with brain.lock:
                    if path == "/audio/transcriptions":
                        self._transcribe(body)
                    elif path == "/chat/completions":
                        self._chat(body)
                    elif path == "/audio/speech":
                        self._speak(body)
                    else:
                        self._send(404, error_body("no such endpoint", "not_found"))
            except Exception as error:  # Report, never crash the server.
                brain.say("  ! %s failed: %s" % (path, error))
                if path == "/audio/speech":
                    # An EMPTY body, not error JSON: the box would try to play
                    # JSON as MP3. Empty means "no audio", which it handles by
                    # showing the words on screen only.
                    self._send(503, raw=b"", content_type="audio/mpeg")
                else:
                    self._send(500, error_body(str(error)))

        def _transcribe(self, body):
            if brain.ears is None:
                raise RuntimeError("hearing is not set up (faster-whisper missing)")
            fields = parse_multipart(body, self.headers.get("Content-Type", ""))
            if "file" not in fields:
                raise ValueError("no audio file in the request")
            language = fields.get("language", b"").decode("utf-8", "replace").strip()
            started = time.time()
            text = brain.ears.transcribe(fields["file"], language)
            brain.say("heard  (%.1f s): %s" % (time.time() - started, text or "(nothing)"))
            self._send(200, {"text": text})

        def _chat(self, body):
            if brain.mind is None:
                raise RuntimeError("thinking is not set up (Ollama missing)")
            request = json.loads(body.decode("utf-8"))
            messages = request.get("messages")
            if not isinstance(messages, list) or not messages:
                raise ValueError("no messages in the request")
            started = time.time()
            text, used = brain.mind.reply(messages, request.get("max_tokens"),
                                          request.get("temperature"))
            brain.say("answer (%.1f s): %s" % (time.time() - started, text))
            self._send(200, chat_completion_body(text, brain.mind.model, used))

        def _speak(self, body):
            if brain.voice is None:
                raise RuntimeError("speaking is not set up")
            request = json.loads(body.decode("utf-8"))
            text = str(request.get("input", "")).strip()
            if not text:
                raise ValueError("nothing to say")
            started = time.time()
            audio = brain.voice.mp3(text[:600])
            brain.say("voice  (%.1f s): %d bytes of MP3" % (time.time() - started, len(audio)))
            self._send(200, raw=audio, content_type="audio/mpeg")

    return Handler


# --------------------------------------------------------------------------- #
# Starting up                                                                  #
# --------------------------------------------------------------------------- #

def lan_address():
    """This computer's address on the local network, as the box will see it."""
    probe = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        # No packet is sent; this only asks the OS which interface it would use.
        probe.connect(("192.168.0.1", 9))
        return probe.getsockname()[0]
    except OSError:
        return "127.0.0.1"
    finally:
        probe.close()


def build_brain(args):
    """Load what is available, and say plainly what is not."""
    problems = []

    ears = mind = voice = None
    try:
        ears = WhisperEars(args.whisper_model, args.device)
    except ImportError:
        problems.append("hearing: pip install faster-whisper")
    except RuntimeError as error:
        problems.append("hearing: %s" % error)

    try:
        mind = OllamaMind(args.ollama, args.model, args.max_tokens)
        mind.check()
    except RuntimeError as error:
        problems.append("thinking: %s" % error)
        mind = None

    try:
        voice = Voice(args.piper_voice)
    except ImportError:
        problems.append("speaking: pip install piper-tts, or leave out --piper-voice")
    except RuntimeError as error:
        problems.append("speaking: %s" % error)

    return HomeBrain(ears, mind, voice, quiet=args.quiet), problems


def main(argv=None):
    parser = argparse.ArgumentParser(description="The Desk Buddy's home AI server.")
    parser.add_argument("--port", type=int, default=DEFAULT_PORT)
    parser.add_argument("--bind", default="0.0.0.0",
                        help="address to listen on (default: every interface)")
    parser.add_argument("--ollama", default=DEFAULT_OLLAMA, help="where Ollama is running")
    parser.add_argument("--model", default=DEFAULT_MODEL,
                        help="Ollama model that answers (default: %s)" % DEFAULT_MODEL)
    parser.add_argument("--max-tokens", type=int, default=120,
                        help="longest answer, in tokens (default: 120)")
    parser.add_argument("--whisper-model", default=DEFAULT_WHISPER,
                        help="faster-whisper model (default: %s)" % DEFAULT_WHISPER)
    parser.add_argument("--device", default="auto", choices=["auto", "cuda", "cpu"])
    parser.add_argument("--piper-voice", default=None,
                        help="a Piper .onnx voice; without it the computer's own voice is used")
    parser.add_argument("--quiet", action="store_true", help="do not print questions and answers")
    args = parser.parse_args(argv)

    brain, problems = build_brain(args)

    print("Desk Buddy home brain")
    print("  hears : %s" % (brain.ears.description if brain.ears else "NOT AVAILABLE"))
    print("  thinks: %s" % (brain.mind.description if brain.mind else "NOT AVAILABLE"))
    print("  speaks: %s" % (brain.voice.description if brain.voice else "NOT AVAILABLE"))
    for problem in problems:
        print("  fix   -> %s" % problem)
    if brain.ears is None or brain.mind is None:
        print("\nThe box needs hearing and thinking to answer questions; fix the lines above.")
        return 1

    # Load the model now rather than on the first question: a cold start takes
    # around ten seconds, which would eat most of the box's request deadline.
    print("\nWarming up %s..." % brain.mind.model, flush=True)
    try:
        brain.mind.reply([{"role": "user", "content": "Say hi."}], 1, 0)
    except Exception as error:
        print("  (warm-up failed, the first answer may be slow: %s)" % error)

    server = ThreadingHTTPServer((args.bind, args.port), make_handler(brain))
    print("\nOn the box: Settings > When to ask > Home AI, then Home AI address:")
    print("    %s:%d" % (lan_address(), args.port))
    print("Press Ctrl+C to stop.\n")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
