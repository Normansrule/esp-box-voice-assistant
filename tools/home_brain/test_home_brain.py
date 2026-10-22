#!/usr/bin/env python3
"""
test_home_brain.py — Checks the home brain speaks the wire format the box expects.

SPDX-License-Identifier: Unlicense OR CC0-1.0

No models needed: hearing, thinking and speaking are replaced by stand-ins, and
what is tested is the part the firmware depends on — the endpoints, the
multipart parsing, and the exact JSON fields the box's OpenAI client insists on.

    python tools/home_brain/test_home_brain.py
"""

import json
import os
import sys
import threading
import unittest
import urllib.error
import urllib.request

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import home_brain  # noqa: E402

# The exact boundary and field layout the firmware's OpenAI component sends.
BOUNDARY = "----WebKitFormBoundary9HKFexBRLrf9dcpY"


def firmware_style_upload(audio, language="en"):
    prefix = "--%s\r\nContent-Disposition: form-data; name=" % BOUNDARY
    body = (prefix + '"model"\r\n\r\nwhisper-1\r\n'
            + prefix + '"temperature"\r\n\r\n0.200000\r\n'
            + prefix + '"language"\r\n\r\n%s\r\n' % language
            + prefix + '"file"; filename="audio.wav"\r\nContent-Type: audio/wav\r\n\r\n')
    return body.encode() + audio + ("\r\n--%s--\r\n" % BOUNDARY).encode()


class FakeEars:
    description = "fake ears"

    def __init__(self):
        self.heard = None

    def transcribe(self, audio, language):
        self.heard = (audio, language)
        return "what is the tallest mountain"


class FakeMind:
    description = "fake mind"
    model = "fake-model"

    def __init__(self):
        self.messages = None

    def reply(self, messages, max_tokens, temperature):
        self.messages = messages
        return "[curious] Mount Everest, at about 8,849 metres.", 42


class FakeVoice:
    description = "fake voice"

    def mp3(self, text):
        if text == "fail":
            raise RuntimeError("voice broke")
        return b"ID3fake-mp3:" + text.encode()


class HomeBrainWireFormat(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.ears, cls.mind = FakeEars(), FakeMind()
        brain = home_brain.HomeBrain(cls.ears, cls.mind, FakeVoice(), quiet=True)
        cls.server = home_brain.ThreadingHTTPServer(("127.0.0.1", 0),
                                                    home_brain.make_handler(brain))
        cls.base = "http://127.0.0.1:%d/v1/" % cls.server.server_address[1]
        threading.Thread(target=cls.server.serve_forever, daemon=True).start()

    @classmethod
    def tearDownClass(cls):
        cls.server.shutdown()
        cls.server.server_close()

    def post(self, endpoint, data, content_type):
        request = urllib.request.Request(self.base + endpoint, data=data,
                                         headers={"Content-Type": content_type,
                                                  "Authorization": "Bearer no-key-needed"})
        try:
            with urllib.request.urlopen(request, timeout=5) as reply:
                return reply.status, reply.read()
        except urllib.error.HTTPError as error:
            return error.code, error.read()

    def test_health_is_what_the_box_probes(self):
        with urllib.request.urlopen(self.base + "health", timeout=5) as reply:
            self.assertEqual(reply.status, 200)
            self.assertTrue(json.load(reply)["ok"])

    def test_transcription_reads_the_firmware_upload(self):
        audio = b"RIFF\x00\x01\x02fake wav\r\n--not-a-boundary"
        status, body = self.post("audio/transcriptions", firmware_style_upload(audio),
                                 "multipart/form-data; boundary=" + BOUNDARY)
        self.assertEqual(status, 200)
        self.assertEqual(json.loads(body)["text"], "what is the tallest mountain")
        self.assertEqual(self.ears.heard, (audio, "en"), "audio bytes were altered in parsing")

    def test_chat_reply_has_every_field_the_firmware_requires(self):
        request = {"model": "home-brain", "max_tokens": 160, "temperature": 0.7,
                   "messages": [{"role": "system", "content": "be a robot"},
                                {"role": "user", "content": "tallest mountain?"}]}
        status, body = self.post("chat/completions", json.dumps(request).encode(),
                                 "application/json")
        self.assertEqual(status, 200)
        reply = json.loads(body)
        # OpenAI.c refuses a reply without usage.total_tokens, then reads
        # choices[0].message.content.
        self.assertIn("total_tokens", reply["usage"])
        self.assertEqual(reply["choices"][0]["message"]["content"],
                         "[curious] Mount Everest, at about 8,849 metres.")
        self.assertEqual(self.mind.messages[0]["role"], "system")

    def test_speech_is_mp3(self):
        status, body = self.post("audio/speech",
                                 json.dumps({"model": "home-voice", "input": "hello",
                                             "voice": "nova"}).encode(),
                                 "application/json")
        self.assertEqual(status, 200)
        self.assertTrue(body.startswith(b"ID3"))

    def test_failed_speech_is_empty_not_json(self):
        # JSON here would be handed to the MP3 decoder; empty means "no audio".
        status, body = self.post("audio/speech", json.dumps({"input": "fail"}).encode(),
                                 "application/json")
        self.assertEqual(status, 503)
        self.assertEqual(body, b"")

    def test_errors_use_the_shape_the_box_recognises(self):
        status, body = self.post("chat/completions", b"{}", "application/json")
        self.assertEqual(status, 500)
        self.assertIn('"error"', body.decode(), "the box would read this aloud")

    def test_paths_work_without_the_v1_prefix(self):
        url = self.base.replace("/v1/", "/") + "health"
        with urllib.request.urlopen(url, timeout=5) as reply:
            self.assertEqual(reply.status, 200)


if __name__ == "__main__":
    unittest.main(verbosity=2)
