#!/usr/bin/env python3
"""
check_secrets.py — Refuse to let keys, passwords and private files into git.

SPDX-License-Identifier: Unlicense OR CC0-1.0

Runs in three places:

  * the pre-commit hook (.githooks/pre-commit, or pre-commit.com via
    .pre-commit-config.yaml), on what you are about to commit:
        python scripts/check_secrets.py --staged
  * CI and the release workflow, on every tracked file:
        python scripts/check_secrets.py
  * by hand, on any files you name:
        python scripts/check_secrets.py path/to/file.c other/file.txt

It looks for two things:

  1. Files that should never be in the repository at all, whatever is in them:
     spiffs/openai_key.txt, sdkconfig, .env files, private keys and so on.
  2. Text that looks like a real credential: OpenAI/Anthropic "sk-" keys,
     GitHub tokens, AWS and Google keys, Slack tokens, PEM private keys, and a
     Wi-Fi password written into a config line.

A line that is a deliberate, harmless example (a fake key in a test, say) can
carry the marker "secret-scan: allow" and is then skipped. Use it sparingly and
never on a real key: if a real key was ever committed, rotate it. Deleting it
from the next commit does not remove it from git history.

Exit status: 0 clean, 1 something found, 2 could not run (not a git checkout).
Standard library only, so it works in any Python 3.8+ without installing
anything, including the Python bundled with the ESP-IDF Windows installer.
"""

import argparse
import os
import fnmatch
import re
import subprocess
import sys
from pathlib import Path

ALLOW_MARKER = "secret-scan: allow"
MAX_BYTES = 2 * 1024 * 1024  # bigger files are assets, not source

# Paths (matched against the repo-relative path with forward slashes, and
# against the bare file name) that must never be committed.
FORBIDDEN_PATHS = [
    "spiffs/openai_key.txt",
    "sdkconfig",
    "sdkconfig.old",
    ".env",
    ".env.*",
    "*.pem",
    "*.key",
    "*.p12",
    "*.pfx",
    "id_rsa*",
    "id_ed25519*",
    "secrets.*",
    "wifi_credentials*",
    "flash_encryption_key*.bin",
    "*signing_key*",
]

# Files that are allowed even though a forbidden pattern matches them.
ALLOWED_PATHS = [
    "sdkconfig.defaults",
    "sdkconfig.defaults.*",
    "spiffs/openai_key.example.txt",
    ".env.example",
]

# Files whose contents are not scanned (machine-written, or huge lock files
# full of hashes that look random).
SKIP_CONTENT = [
    "*.lock",
    "package-lock.json",
]

# (name, compiled regex). Each pattern starts with a boundary so that words
# like "desk-buddy" do not read as "sk-" keys.
PATTERNS = [
    ("OpenAI/Anthropic API key",
     re.compile(r"(?<![A-Za-z0-9_-])sk-(?:proj-|svcacct-|admin-|ant-[a-z0-9]+-)?[A-Za-z0-9_-]{20,}")),
    ("GitHub token",
     re.compile(r"(?<![A-Za-z0-9])(?:gh[pousr]_[A-Za-z0-9]{36,}|github_pat_[A-Za-z0-9_]{50,})")),
    ("AWS access key id",
     re.compile(r"(?<![A-Z0-9])(?:AKIA|ASIA)[0-9A-Z]{16}(?![A-Z0-9])")),
    ("Google API key",
     re.compile(r"(?<![A-Za-z0-9])AIza[0-9A-Za-z_-]{35}")),
    ("Slack token",
     re.compile(r"(?<![A-Za-z0-9])xox[abposr]-[A-Za-z0-9-]{10,}")),
    ("Private key block",
     re.compile(r"-----BEGIN (?:[A-Z]+ )*PRIVATE KEY-----")),
    ("Wi-Fi password in config",
     re.compile(r"CONFIG_[A-Z0-9_]*WIFI_PASSWORD\s*=\s*\"[^\"]+\"")),
    ("Wi-Fi password in a #define",
     re.compile(r"#define\s+\w*PASS(?:WORD|WD)\w*\s+\"[^\"\s]{8,}\"")),
    ("Wi-Fi password in source",
     re.compile(r"(?i)\b(?:wifi_)?pass(?:word|wd)\b\s*[:=]\s*\"[^\"\s]{8,}\"")),
]


def git(*args, binary=False):
    result = subprocess.run(["git", *args], capture_output=True)
    if result.returncode != 0:
        raise RuntimeError(result.stderr.decode(errors="replace").strip())
    return result.stdout if binary else result.stdout.decode(errors="replace")


def matches(path, patterns):
    name = path.rsplit("/", 1)[-1]
    return any(fnmatch.fnmatch(path, p) or fnmatch.fnmatch(name, p) for p in patterns)


def is_forbidden(path):
    return matches(path, FORBIDDEN_PATHS) and not matches(path, ALLOWED_PATHS)


def scan_text(path, data):
    """Return a list of (line number, finding name) for one file's bytes."""
    if b"\0" in data[:8192] or len(data) > MAX_BYTES:
        return []  # binary or an asset
    findings = []
    text = data.decode("utf-8", errors="replace")
    for number, line in enumerate(text.splitlines(), 1):
        if ALLOW_MARKER in line:
            continue
        for name, pattern in PATTERNS:
            if pattern.search(line):
                findings.append((number, name))
    return findings


def collect(mode, paths):
    """Yield (repo-relative path, bytes) for the files to check."""
    if mode == "staged":
        names = git("diff", "--cached", "--name-only", "--diff-filter=ACMR", "-z").split("\0")
        for name in filter(None, names):
            yield name, git("show", f":{name}", binary=True)
    else:
        names = paths or [n for n in git("ls-files", "-z").split("\0") if n]
        for name in names:
            file = Path(name)
            if file.is_file():
                yield file.as_posix(), file.read_bytes()


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--staged", action="store_true",
                        help="check only what is staged for the next commit")
    parser.add_argument("paths", nargs="*", help="files to check (default: every tracked file)")
    args = parser.parse_args()

    try:
        root = git("rev-parse", "--show-toplevel").strip()
    except (RuntimeError, FileNotFoundError) as error:
        print(f"check_secrets: needs git and a git checkout ({error})", file=sys.stderr)
        return 2

    # Named paths are relative to where you ran it; everything else to the root.
    paths = [Path(p).resolve().relative_to(Path(root).resolve()).as_posix() for p in args.paths]
    os.chdir(root)

    problems = []
    checked = 0
    for path, data in collect("staged" if args.staged else "tree", paths):
        checked += 1
        if is_forbidden(path):
            problems.append(f"{path}: this file must not be committed (see .gitignore)")
            continue
        if matches(path, SKIP_CONTENT):
            continue
        for line, name in scan_text(path, data):
            problems.append(f"{path}:{line}: looks like a {name}")

    if problems:
        print("check_secrets: possible secrets found\n", file=sys.stderr)
        for problem in problems:
            print("  " + problem, file=sys.stderr)
        print(
            "\nIf this is a real key or password: remove it, keep it in a git-ignored file,"
            "\nand ROTATE it (make a new one, revoke the old) if it was ever pushed."
            "\nTo unstage a file:   git restore --staged <file>"
            f"\nIf it is a harmless example, add the comment \"{ALLOW_MARKER}\" to that line.",
            file=sys.stderr,
        )
        return 1

    print(f"check_secrets: {checked} file(s) checked, nothing found")
    return 0


if __name__ == "__main__":
    sys.exit(main())
