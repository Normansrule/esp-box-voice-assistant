#!/usr/bin/env python3
"""
test_check_secrets.py — Host tests for scripts/check_secrets.py.

SPDX-License-Identifier: Unlicense OR CC0-1.0

    python tests/test_check_secrets.py

The fake credentials below are built at run time so that this file itself
never contains anything the scanner would flag.
"""

import importlib.util
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
spec = importlib.util.spec_from_file_location("check_secrets", ROOT / "scripts" / "check_secrets.py")
check_secrets = importlib.util.module_from_spec(spec)
spec.loader.exec_module(check_secrets)


def found(text):
    return [name for _, name in check_secrets.scan_text("x.c", text.encode())]


class Patterns(unittest.TestCase):
    def test_openai_project_key(self):
        self.assertTrue(found('k = "' + "sk-" + "proj-" + "A" * 40 + '"'))

    def test_classic_openai_key(self):
        self.assertTrue(found("sk-" + "a1" * 24))

    def test_github_token(self):
        self.assertTrue(found("ghp" + "_" + "b" * 36))

    def test_private_key(self):
        self.assertTrue(found("-----BEGIN RSA " + "PRIVATE KEY-----"))

    def test_wifi_password_define(self):
        self.assertTrue(found('#define MY_WIFI_PASS' + 'WORD "hunter2hunter2"'))

    def test_wifi_password_config(self):
        self.assertTrue(found('CONFIG_EXAMPLE_WIFI_PASS' + 'WORD="hunter2"'))

    def test_desk_buddy_is_not_a_key(self):
        # The old CI regex flagged this workflow name as an "sk-" key.
        self.assertEqual(found("name: desk-buddy-connect-windows-installer"), [])

    def test_placeholder_and_struct_field_are_fine(self):
        self.assertEqual(found("PASTE-YOUR-OPENAI-PROJECT-KEY-ON-THIS-LINE"), [])
        self.assertEqual(found("char wifi_password[BUDDY_WIFI_PASSWORD_MAX_LEN];"), [])

    def test_allow_marker(self):
        self.assertEqual(found("sk-" + "a1" * 24 + "  // secret-scan: allow"), [])

    def test_binary_is_skipped(self):
        data = b"\0\1\2" + ("sk-" + "a1" * 24).encode()
        self.assertEqual(check_secrets.scan_text("x.bin", data), [])


class Paths(unittest.TestCase):
    def test_forbidden(self):
        for path in ["spiffs/openai_key.txt", "sdkconfig", "sdkconfig.old", ".env",
                     "certs/server.pem", "flash_encryption_key.bin", "secure_boot_signing_key.pem"]:
            self.assertTrue(check_secrets.is_forbidden(path), path)

    def test_allowed(self):
        for path in ["sdkconfig.defaults", "spiffs/openai_key.example.txt",
                     "main/core/buddy_api_key.c", "main/core/buddy_api_key.h"]:
            self.assertFalse(check_secrets.is_forbidden(path), path)


if __name__ == "__main__":
    sys.exit(unittest.main())
