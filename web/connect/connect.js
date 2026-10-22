/*
 * Desk Buddy Connect - flash, monitor and control an ESP32-S3-BOX-3 over USB.
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 *
 * One serial port, three uses:
 *   Install  esptool-js takes the port, writes the images, resets the chip.
 *   Monitor  the page reads the port line by line and shows the log.
 *   Control  the page writes commands for main/core/buddy_console.c and picks
 *            its "@buddy {json}" answers out of the same stream.
 *
 * The same file runs inside the desktop app (desktop/). There the only
 * difference is the port picker: Electron has no built-in chooser, so it hands
 * the list to window.deskBuddyDesktop and this page draws one.
 */

import { ESPLoader, Transport } from "esptool-js";

const $ = (id) => document.getElementById(id);
const MONITOR_BAUD = 115200;      // USB-Serial-JTAG ignores it, a USB-UART does not
const FLASH_BAUD = 921600;
const TERM_MAX_LINES = 5000;
const REPLY_PREFIX = "@buddy ";

/*
 * Where each image goes when no flasher_args.json says otherwise. Taken from
 * build/flasher_args.json for v3.7.1, which follows partitions.csv.
 */
const DEFAULT_LAYOUT = {
  "bootloader.bin":         { role: "bootloader",      offset: 0x0 },
  "partition-table.bin":    { role: "partition-table", offset: 0x8000 },
  "ota_data_initial.bin":   { role: "otadata",         offset: 0x16000 },
  "esp_box_desk_buddy.bin": { role: "app",             offset: 0x20000 },
  "storage.bin":            { role: "storage",         offset: 0x3a4000 },
  "srmodels.bin":           { role: "model",           offset: 0x79a000 },
};

const state = {
  port: null,          // SerialPort
  reader: null,        // active ReadableStreamDefaultReader while monitoring
  monitoring: false,
  busy: false,         // flashing
  release: null,       // manifest.json, when one is published
  parts: [],           // [{name, role, offset, size, data?: Uint8Array, url?, sha256?}]
  source: "release",
  consoleSeen: false,
};

/* ------------------------------------------------------------------ */
/* Small helpers                                                       */
/* ------------------------------------------------------------------ */

const hex = (n) => "0x" + n.toString(16);
const kb = (n) => n >= 1048576 ? (n / 1048576).toFixed(2) + " MB" : Math.ceil(n / 1024) + " KB";
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
const escapeHtml = (s) => String(s).replace(/[&<>"]/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;" }[c]));

function setLink(text, mode) {
  const chip = $("link-state");
  chip.textContent = text;
  chip.dataset.state = mode;
}

async function sha256Hex(bytes) {
  const digest = await crypto.subtle.digest("SHA-256", bytes);
  return [...new Uint8Array(digest)].map((b) => b.toString(16).padStart(2, "0")).join("");
}

/*
 * MD5, for esptool-js's read-back check: after each image is written the
 * flasher stub hashes what landed in flash and the two must agree. WebCrypto
 * has no MD5, so here it is (RFC 1321).
 */
function md5Hex(bytes) {
  const K = new Uint32Array(64), S = [7, 12, 17, 22, 5, 9, 14, 20, 4, 11, 16, 23, 6, 10, 15, 21];
  for (let i = 0; i < 64; i++) K[i] = Math.floor(Math.abs(Math.sin(i + 1)) * 2 ** 32) >>> 0;
  const len = bytes.length, padded = new Uint8Array(((len + 72) >>> 6) << 6);
  padded.set(bytes); padded[len] = 0x80;
  const view = new DataView(padded.buffer);
  view.setUint32(padded.length - 8, (len << 3) >>> 0, true);
  view.setUint32(padded.length - 4, Math.floor(len / 0x20000000), true);
  let a0 = 0x67452301, b0 = 0xefcdab89, c0 = 0x98badcfe, d0 = 0x10325476;
  const M = new Uint32Array(16);
  for (let off = 0; off < padded.length; off += 64) {
    for (let j = 0; j < 16; j++) M[j] = view.getUint32(off + j * 4, true);
    let A = a0, B = b0, C = c0, D = d0;
    for (let i = 0; i < 64; i++) {
      let F, g;
      if (i < 16) { F = (B & C) | (~B & D); g = i; }
      else if (i < 32) { F = (D & B) | (~D & C); g = (5 * i + 1) & 15; }
      else if (i < 48) { F = B ^ C ^ D; g = (3 * i + 5) & 15; }
      else { F = C ^ (B | ~D); g = (7 * i) & 15; }
      const s = S[(i >> 4) * 4 + (i & 3)];
      F = (F + A + K[i] + M[g]) >>> 0;
      A = D; D = C; C = B;
      B = (B + ((F << s) | (F >>> (32 - s)))) >>> 0;
    }
    a0 = (a0 + A) >>> 0; b0 = (b0 + B) >>> 0; c0 = (c0 + C) >>> 0; d0 = (d0 + D) >>> 0;
  }
  return [a0, b0, c0, d0].map((w) => [0, 8, 16, 24].map((s) => ((w >>> s) & 255).toString(16).padStart(2, "0")).join("")).join("");
}
// For checking the page without a box: feed it serial text from devtools.
window.__deskBuddyTest = { md5Hex, receiveText: (t) => receiveText(t) };

/* ------------------------------------------------------------------ */
/* Tabs                                                                */
/* ------------------------------------------------------------------ */

function showTab(name) {
  for (const t of ["install", "monitor", "control"]) {
    $("tab-" + t).setAttribute("aria-selected", String(t === name));
    $("panel-" + t).hidden = t !== name;
  }
  if (name === "control" && state.monitoring && !state.consoleSeen) send("status");
  try { localStorage.setItem("connect-tab", name); } catch { /* private window */ }
}

/* ------------------------------------------------------------------ */
/* Terminal                                                            */
/* ------------------------------------------------------------------ */

const term = $("term");
let partialLine = "";
let ansiClass = "";

function termLine(text, cls) {
  const line = document.createElement("div");
  line.className = "l" + (cls ? " " + cls : "");
  const stamp = document.createElement("span");
  stamp.className = "t";
  stamp.textContent = new Date().toLocaleTimeString([], { hour12: false });
  stamp.hidden = !$("term-time").checked;
  line.append(stamp, document.createTextNode(text));
  line.dataset.text = text.toLowerCase();
  const filter = $("term-filter").value.trim().toLowerCase();
  if (filter && !line.dataset.text.includes(filter)) line.hidden = true;
  term.append(line);
  while (term.childElementCount > TERM_MAX_LINES) term.firstElementChild.remove();
  if ($("term-scroll").checked) term.scrollTop = term.scrollHeight;
}

const sys = (text) => termLine(text, "sys");

/** ESP-IDF colours its log with SGR codes; keep the colour, drop the code. */
function receiveLine(raw) {
  let cls = ansiClass;
  const text = raw.replace(/\x1b\[([0-9;]*)m/g, (_, codes) => {
    const last = codes.split(";").filter(Boolean).pop() || "0";
    cls = last === "0" ? "" : "c" + last;
    return "";
  }).replace(/\r$/, "");
  ansiClass = cls;
  if (text.startsWith(REPLY_PREFIX)) {
    termLine(text, "rx");
    handleReply(text.slice(REPLY_PREFIX.length));
    return;
  }
  const level = /^([EWI]) \(\d+\)/.exec(text);
  termLine(text, cls || (level ? "lv-" + level[1].toLowerCase() : ""));
  if (/Serial console ready/.test(text)) send("status");
}

function receiveText(chunk) {
  partialLine += chunk;
  const lines = partialLine.split("\n");
  partialLine = lines.pop();
  for (const l of lines) receiveLine(l);
}

/* ------------------------------------------------------------------ */
/* Port                                                                */
/* ------------------------------------------------------------------ */

async function choosePort() {
  if (state.port) await disconnect();
  // No vendor filter: a box behind a USB-UART bridge has a different VID, and
  // the step list on the page names the port to pick.
  try { state.port = await navigator.serial.requestPort(); } catch { return; }
  state.port.addEventListener("disconnect", () => {
    sys("Box unplugged.");
    state.monitoring = false; state.reader = null; state.port = null;
    refreshButtons();
  });
  const info = state.port.getInfo();
  sys(`Port chosen (USB ${hex(info.usbVendorId ?? 0)}:${hex(info.usbProductId ?? 0)}).`);
  await openMonitor();
}

async function disconnect() {
  await closeMonitor();
  try { await state.port?.forget?.(); } catch { /* older browsers */ }
  state.port = null;
  refreshButtons();
}

async function openMonitor() {
  if (!state.port || state.monitoring) return;
  try {
    await state.port.open({ baudRate: MONITOR_BAUD, bufferSize: 1 << 16 });
  } catch (e) {
    sys("Could not open the port: " + e.message + " Close any other program using it (idf.py monitor, Arduino) and try again.");
    refreshButtons();
    return;
  }
  // Opening a port can leave DTR/RTS asserted; on the BOX-3 that combination
  // is the reset and boot-mode wiring, so release both before reading.
  try { await state.port.setSignals({ dataTerminalReady: false, requestToSend: false }); } catch { /* not all ports */ }
  state.monitoring = true;
  refreshButtons();
  sys("Monitor open.");
  readLoop();
  setTimeout(() => send("status"), 400);
}

async function readLoop() {
  const decoder = new TextDecoder();
  while (state.monitoring && state.port?.readable) {
    state.reader = state.port.readable.getReader();
    try {
      while (true) {
        const { value, done } = await state.reader.read();
        if (done) break;
        receiveText(decoder.decode(value, { stream: true }));
      }
    } catch (e) {
      if (state.monitoring) sys("Read error: " + e.message);
    } finally {
      state.reader?.releaseLock();
      state.reader = null;
    }
  }
}

async function closeMonitor() {
  if (!state.monitoring) return;
  state.monitoring = false;
  try { await state.reader?.cancel(); } catch { /* already gone */ }
  for (let i = 0; i < 20 && state.reader; i++) await sleep(25);
  try { await state.port?.close(); } catch { /* already closed */ }
  sys("Monitor closed.");
  refreshButtons();
}

async function send(line) {
  if (!state.monitoring || !state.port?.writable) return false;
  const writer = state.port.writable.getWriter();
  try {
    await writer.write(new TextEncoder().encode(line + "\n"));
    termLine("> " + line, "tx");
    return true;
  } catch (e) {
    sys("Write failed: " + e.message);
    return false;
  } finally {
    writer.releaseLock();
  }
}

/** Pulse EN through RTS, as esptool's hard reset does on this board. */
async function resetBox() {
  if (!state.monitoring) return;
  sys("Resetting...");
  await state.port.setSignals({ requestToSend: true, dataTerminalReady: false });
  await sleep(120);
  await state.port.setSignals({ requestToSend: false, dataTerminalReady: false });
}

/* ------------------------------------------------------------------ */
/* Firmware sources                                                    */
/* ------------------------------------------------------------------ */

async function loadRelease() {
  try {
    const res = await fetch("../firmware/manifest.json", { cache: "no-cache" });
    if (!res.ok) throw new Error(res.status);
    state.release = await res.json();
    $("release-label").textContent = `v${state.release.version}${state.release.date ? ", " + state.release.date : ""}`;
  } catch {
    state.release = null;
    $("release-label").textContent = "(none published yet; use your own build)";
    document.querySelector('input[name="source"][value="local"]').checked = true;
    state.source = "local";
    $("local-pick").hidden = false;
  }
  rebuildParts();
}

async function readLocalFiles(files) {
  const list = [...files];
  let layout = DEFAULT_LAYOUT;
  const args = list.find((f) => f.name === "flasher_args.json");
  if (args) {
    try {
      const json = JSON.parse(await args.text());
      layout = {};
      const roles = { bootloader: "bootloader", "partition-table": "partition-table", otadata: "otadata", app: "app", storage: "storage", model: "model" };
      for (const [key, entry] of Object.entries(json)) {
        if (roles[key] && entry && entry.file) layout[entry.file.split("/").pop()] = { role: roles[key], offset: parseInt(entry.offset, 16) };
      }
      sys("Using offsets from flasher_args.json.");
    } catch (e) {
      sys("flasher_args.json could not be read (" + e.message + "); using the v3.7.1 layout.");
    }
  }
  const bins = list.filter((f) => f.name.endsWith(".bin"));
  const parts = [];
  for (const f of bins) {
    let place = layout[f.name];
    // A single unknown image is a merged one (the factory firmware, or the
    // output of `esptool.py merge_bin`): it starts at the very beginning.
    if (!place && bins.length === 1) place = { role: "merged", offset: 0 };
    if (!place) { sys(`Skipped ${f.name}: not part of this firmware's layout.`); continue; }
    parts.push({ name: f.name, role: place.role, offset: place.offset, size: f.size, data: new Uint8Array(await f.arrayBuffer()) });
  }
  state.localParts = parts.sort((a, b) => a.offset - b.offset);
  rebuildParts();
}

function rebuildParts() {
  if (state.source === "release" && state.release) {
    state.parts = state.release.parts.map((p) => ({ ...p, url: new URL(p.file, new URL("../firmware/", location)).href }));
  } else if (state.source === "local") {
    state.parts = state.localParts || [];
  } else {
    state.parts = [];
  }
  renderParts();
}

function selectedParts() {
  const appOnly = $("opt-app-only").checked;
  return state.parts.filter((p) => !appOnly || p.role === "app");
}

function renderParts() {
  const body = $("parts-body");
  if (!state.parts.length) {
    body.innerHTML = '<tr><td colspan="4" class="muted">No firmware selected yet.</td></tr>';
  } else {
    const chosen = new Set(selectedParts());
    body.innerHTML = state.parts.map((p, i) =>
      `<tr class="${chosen.has(p) ? "" : "skip"}"><td>${escapeHtml(p.name)}</td><td>${hex(p.offset)}</td><td>${kb(p.size)}</td><td id="part-${i}"></td></tr>`
    ).join("");
  }
  refreshButtons();
}

/* ------------------------------------------------------------------ */
/* Install                                                             */
/* ------------------------------------------------------------------ */

function progress(step, fraction) {
  $("flash-progress").hidden = false;
  $("flash-step").textContent = step;
  $("flash-pct").textContent = Math.round(fraction * 100) + "%";
  $("flash-fill").style.width = (fraction * 100).toFixed(1) + "%";
}

async function fetchPart(part) {
  if (part.data) return part.data;
  const res = await fetch(part.url);
  if (!res.ok) throw new Error(`${part.name}: download failed (${res.status})`);
  const data = new Uint8Array(await res.arrayBuffer());
  if (part.sha256 && (await sha256Hex(data)) !== part.sha256) throw new Error(`${part.name}: checksum does not match the release`);
  return data;
}

async function install() {
  const parts = selectedParts();
  if (!state.port || !parts.length || state.busy) return;
  const eraseAll = $("opt-erase").checked;
  if (eraseAll && !confirm("Erase the whole flash first? Saved Wi-Fi, settings and the API key on the box will be deleted.")) return;

  state.busy = true;
  refreshButtons();
  setLink("Installing", "busy");
  showTab("monitor");

  let transport = null;
  try {
    progress("Fetching images", 0);
    const files = [];
    for (let i = 0; i < parts.length; i++) {
      files.push({ data: await fetchPart(parts[i]), address: parts[i].offset });
      progress("Fetching images", (i + 1) / parts.length * 0.05);
    }

    await closeMonitor();
    transport = new Transport(state.port, false);
    const terminal = { clean() {}, writeLine: (t) => sys(t), write: (t) => sys(t) };
    const loader = new ESPLoader({ transport, baudrate: FLASH_BAUD, terminal, debugLogging: false });

    progress("Connecting to the chip", 0.05);
    const chip = await loader.main();
    sys("Chip: " + chip);
    if (!/ESP32-S3/i.test(chip)) throw new Error(`this is a ${chip}, not an ESP32-S3; nothing was written`);

    const total = files.reduce((n, f) => n + f.data.length, 0);
    const done = files.map(() => 0);
    await loader.writeFlash({
      fileArray: files,
      flashSize: "keep", flashMode: "keep", flashFreq: "keep",
      eraseAll,
      compress: true,
      reportProgress: (index, written) => {
        done[index] = written;
        const sum = done.reduce((a, b) => a + b, 0);
        progress(`Writing ${parts[index].name}`, 0.05 + 0.95 * sum / total);
        if (written >= files[index].data.length) $("part-" + state.parts.indexOf(parts[index])).innerHTML = '<span class="tick">written</span>';
      },
      calculateMD5Hash: md5Hex,
    });
    progress("Done, restarting the box", 1);
    await loader.after("hard_reset");
    sys("Firmware written and verified. The box is restarting.");
  } catch (e) {
    sys("Install failed: " + (e?.message || e));
    progress("Failed: " + (e?.message || e), 0);
  } finally {
    try { await transport?.disconnect(); } catch { /* port already closed */ }
    state.busy = false;
    setLink("Connected", "on");
    await sleep(300);
    if (state.port) await openMonitor();
    refreshButtons();
  }
}

/* ------------------------------------------------------------------ */
/* Control                                                             */
/* ------------------------------------------------------------------ */

function stat(label, value) {
  return `<dt>${escapeHtml(label)}</dt><dd>${value === undefined || value === null ? "UNKNOWN" : escapeHtml(value)}</dd>`;
}

function handleReply(json) {
  let r;
  try { r = JSON.parse(json); } catch { return; }
  if (!state.consoleSeen) {
    state.consoleSeen = true;
    $("console-missing").hidden = true;
    send("phrases").then(() => send("faces"));
    refreshButtons();
  }
  if (!r.ok && r.error) sys(`${r.cmd || "?"}: ${r.error}`);

  switch (r.cmd) {
  case "status": {
    const up = r.uptime_s;
    $("stats").innerHTML =
      stat("Firmware", r.version && `${r.firmware} ${r.version}`) +
      stat("Up for", up === undefined ? undefined : `${Math.floor(up / 3600)}h ${Math.floor(up / 60) % 60}m ${up % 60}s`) +
      stat("Last reset", r.reset_reason) +
      stat("Free RAM", r.heap_internal && `${kb(r.heap_internal)} (lowest ${kb(r.heap_internal_min)})`) +
      stat("Free PSRAM", r.heap_psram && kb(r.heap_psram)) +
      stat("Wi-Fi", r.wifi && (r.ip ? `${r.wifi}, ${r.ip}` : r.wifi)) +
      stat("Brain", r.brain && `${r.brain}${r.cloud_key ? ", key set" : ", no key"}`) +
      stat("Cloud left", r.cloud_requests_left) +
      stat("Timer", r.timer_s ? `${Math.floor(r.timer_s / 60)}:${String(r.timer_s % 60).padStart(2, "0")}` : "off") +
      stat("Sensor dock", r.dock === undefined ? undefined : r.dock ? "attached" : "not attached") +
      (r.temperature_c !== undefined ? stat("Room", `${r.temperature_c} °C, ${r.humidity}% RH`) : "");
    if (r.volume !== undefined) { $("vol").value = r.volume; $("vol-out").textContent = r.volume + "%"; }
    if (r.brightness !== undefined) { $("bri").value = r.brightness; $("bri-out").textContent = r.brightness + "%"; }
    break;
  }
  case "selftest":
    $("checks").innerHTML = (r.checks || []).map((c) =>
      `<li class="${c.result}"><b>${c.result}</b><span>${escapeHtml(c.name)}: ${escapeHtml(c.detail)}</span></li>`).join("");
    break;
  case "phrases":
    renderPills("phrases", r.phrases || [], (p) => send("say " + p));
    break;
  case "faces":
    renderPills("faces", r.faces || [], (f) => send("face " + f));
    break;
  case "volume": $("vol-out").textContent = r.volume + "%"; break;
  case "brightness": $("bri-out").textContent = r.brightness + "%"; break;
  }
}

function renderPills(id, items, onPick) {
  const box = $(id);
  box.replaceChildren(...items.map((item) => {
    const b = document.createElement("button");
    b.type = "button";
    b.textContent = item;
    b.addEventListener("click", () => onPick(item));
    return b;
  }));
  if (id === "phrases") filterPhrases();
}

function filterPhrases() {
  const q = $("phrase-filter").value.trim().toLowerCase();
  for (const b of $("phrases").querySelectorAll("button")) b.hidden = q && !b.textContent.toLowerCase().includes(q);
}

/* ------------------------------------------------------------------ */
/* Buttons                                                             */
/* ------------------------------------------------------------------ */

function refreshButtons() {
  const live = state.monitoring && !state.busy;
  const ctl = live && state.consoleSeen;
  $("btn-connect").textContent = state.port ? "Disconnect" : "Connect box";
  $("btn-connect").disabled = state.busy;
  $("btn-flash").disabled = !state.port || state.busy || !selectedParts().length;
  $("btn-reset").disabled = !live;
  $("term-input").disabled = $("term-send").disabled = !live;
  for (const id of ["btn-status", "vol", "bri", "btn-timer", "btn-timer-cancel", "btn-selftest", "btn-reboot"]) $(id).disabled = !ctl;
  if (!state.busy) setLink(state.port ? (state.monitoring ? "Connected" : "Port chosen") : "Not connected", state.port ? "on" : "off");
}

function debounce(fn, ms) {
  let t;
  return (...a) => { clearTimeout(t); t = setTimeout(() => fn(...a), ms); };
}

/* ------------------------------------------------------------------ */
/* Desktop app port picker                                             */
/* ------------------------------------------------------------------ */

function wireDesktopPicker() {
  const bridge = window.deskBuddyDesktop;
  if (!bridge) return;
  const dialog = $("port-dialog");
  bridge.onPortList((ports) => {
    const list = $("port-list");
    list.replaceChildren(...ports.map((p) => {
      const li = document.createElement("li");
      const b = document.createElement("button");
      b.type = "button";
      b.innerHTML = `${escapeHtml(p.displayName || p.portName)}<small>${escapeHtml(p.portName)}${p.vendorId ? " · USB " + escapeHtml(p.vendorId) + ":" + escapeHtml(p.productId) : ""}</small>`;
      b.addEventListener("click", () => { bridge.choosePort(p.portId); dialog.close("picked"); });
      li.append(b);
      return li;
    }));
    if (!ports.length) list.innerHTML = '<li class="muted">No serial ports found. Plug the box in with a data cable.</li>';
    if (!dialog.open) dialog.showModal();
  });
  dialog.addEventListener("close", () => { if (dialog.returnValue !== "picked") bridge.choosePort(""); });
}

/* ------------------------------------------------------------------ */
/* Start                                                               */
/* ------------------------------------------------------------------ */

function start() {
  if (!("serial" in navigator)) {
    $("no-serial").hidden = false;
    $("btn-connect").disabled = true;
  }
  wireDesktopPicker();

  for (const t of ["install", "monitor", "control"]) $("tab-" + t).addEventListener("click", () => showTab(t));
  let saved = "install";
  try { saved = localStorage.getItem("connect-tab") || saved; } catch { /* private window */ }
  showTab(saved);

  $("btn-connect").addEventListener("click", () => (state.port ? disconnect() : choosePort()));
  for (const r of document.querySelectorAll('input[name="source"]')) r.addEventListener("change", () => {
    state.source = r.value;
    $("local-pick").hidden = r.value !== "local";
    rebuildParts();
  });
  $("local-files").addEventListener("change", (e) => readLocalFiles(e.target.files));
  $("opt-app-only").addEventListener("change", renderParts);
  $("btn-flash").addEventListener("click", install);

  $("btn-reset").addEventListener("click", resetBox);
  $("btn-clear").addEventListener("click", () => term.replaceChildren());
  $("btn-save").addEventListener("click", () => {
    const text = [...term.children].map((l) => l.dataset.text !== undefined ? l.lastChild.textContent : "").join("\n");
    const a = document.createElement("a");
    a.href = URL.createObjectURL(new Blob([text + "\n"], { type: "text/plain" }));
    a.download = `desk-buddy-log-${new Date().toISOString().replace(/[:.]/g, "-")}.txt`;
    a.click();
    setTimeout(() => URL.revokeObjectURL(a.href), 1000);
  });
  $("term-filter").addEventListener("input", () => {
    const q = $("term-filter").value.trim().toLowerCase();
    for (const l of term.children) l.hidden = q && !l.dataset.text.includes(q);
  });
  $("term-time").addEventListener("change", () => {
    for (const s of term.querySelectorAll(".t")) s.hidden = !$("term-time").checked;
  });
  $("term-form").addEventListener("submit", (e) => {
    e.preventDefault();
    const line = $("term-input").value.trim();
    if (line) send(line);
    $("term-input").value = "";
  });

  $("btn-status").addEventListener("click", () => send("status"));
  $("btn-selftest").addEventListener("click", () => send("selftest"));
  $("btn-reboot").addEventListener("click", () => send("reboot"));
  $("btn-timer").addEventListener("click", () => send("timer " + $("timer-min").value));
  $("btn-timer-cancel").addEventListener("click", () => send("timer cancel"));
  $("vol").addEventListener("input", debounce(() => send("volume " + $("vol").value), 250));
  $("bri").addEventListener("input", debounce(() => send("brightness " + $("bri").value), 250));
  $("phrase-filter").addEventListener("input", filterPhrases);

  sys("Ready. Press Connect box.");
  loadRelease();
  refreshButtons();
}

start();
