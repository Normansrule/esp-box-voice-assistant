/*
 * Checks that need no window and no box: run after scripts/prepare.mjs.
 *
 *   - the bundled page loads esptool-js from inside the app, not a CDN
 *   - the page's MD5 (used to verify every write) agrees with Node's
 *   - bundled firmware, if any, matches its manifest and carries no API key
 *   - the preload exposes exactly what the page expects
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */

import assert from "node:assert/strict";
import { createHash } from "node:crypto";
import { existsSync, readFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";

const here = dirname(dirname(fileURLToPath(import.meta.url)));
const app = join(here, "app");
let passed = 0;
const check = (name, fn) => { fn(); passed++; console.log("ok  " + name); };

check("page uses the bundled esptool-js", () => {
  const html = readFileSync(join(app, "connect", "index.html"), "utf8");
  assert.match(html, /"esptool-js": "\.\/vendor\/esptool-js\.js"/);
  assert.doesNotMatch(html, /<script[^>]+src="https?:/);
  assert.ok(existsSync(join(app, "connect", "vendor", "esptool-js.js")));
});

check("MD5 used for write verification matches Node", () => {
  const source = readFileSync(join(app, "connect", "connect.js"), "utf8");
  const body = source.match(/function md5Hex\(bytes\) \{[\s\S]*?\n\}\n/)[0];
  const md5Hex = new Function(body + "; return md5Hex;")();
  const samples = ["", "abc", "a".repeat(55), "a".repeat(56), "a".repeat(64), "a".repeat(1000)]
    .map((s) => new TextEncoder().encode(s));
  samples.push(Uint8Array.from({ length: 300001 }, (_, i) => (i * 7919) & 255));
  for (const s of samples) assert.equal(md5Hex(s), createHash("md5").update(s).digest("hex"));
});

check("bundled firmware matches its manifest", () => {
  const dir = join(app, "firmware");
  if (!existsSync(join(dir, "manifest.json"))) { console.log("    (no firmware bundled)"); return; }
  const manifest = JSON.parse(readFileSync(join(dir, "manifest.json"), "utf8"));
  assert.ok(manifest.parts.some((p) => p.role === "app"));
  for (const part of manifest.parts) {
    const data = readFileSync(join(dir, part.file));
    assert.equal(data.length, part.size, part.file + " size");
    assert.equal(createHash("sha256").update(data).digest("hex"), part.sha256, part.file + " sha256");
    assert.doesNotMatch(data.toString("latin1"), /sk-(proj-)?[A-Za-z0-9_-]{20,}/, part.file + " contains a key");
  }
});

check("preload exposes onPortList and choosePort", () => {
  const preload = readFileSync(join(here, "preload.js"), "utf8");
  const page = readFileSync(join(app, "connect", "connect.js"), "utf8");
  for (const name of ["onPortList", "choosePort"]) {
    assert.match(preload, new RegExp(name + ":"));
    assert.match(page, new RegExp("bridge\\." + name));
  }
});

console.log(`\n${passed} checks passed`);
