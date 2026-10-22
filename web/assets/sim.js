/*
 * sim.js - the Desk Buddy simulator: screen compositor, voice flow, settings,
 * sensors, cloud-router port and the page's scroll choreography.
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 *
 * Everything here is a SIMULATION that runs in the browser. Replies, faces,
 * timings and voice clips come from the firmware source (via
 * firmware-data.js); sensor values are inputs the visitor sets. Nothing is
 * read from, or sent to, a device or a cloud service.
 */
(function () {
  "use strict";

  const FW = window.FIRMWARE;
  const F = window.DeskBuddyFace;
  const G = FW.geometry;
  const T = FW.theme;
  const $ = (s, el) => (el || document).querySelector(s);
  const $$ = (s, el) => Array.from((el || document).querySelectorAll(s));
  const reduceMotion = window.matchMedia("(prefers-reduced-motion: reduce)").matches;

  /* Constants that live in C sources rather than the extracted tables. */
  const EDGE = 6;                       /* BUDDY_SCREEN_EDGE_MARGIN */
  const GEAR = 38;                      /* BUDDY_SETTINGS_BUTTON_SIZE */
  const BUBBLE_PAD = 9;                 /* BUDDY_SPEECH_BUBBLE_PADDING */
  const BUBBLE_MAX_W = G.BUDDY_SCREEN_WIDTH - 2 * EDGE;
  const BUBBLE_MIN_W = 64;
  const LIFT_DIVISOR = 3, LIFT_MAX = 34, SLIDE_MS = 220;
  const CAPTION_LINGER_MS = 5000, LISTENING_CAPTION_MS = 8000;
  const LOUDNESS_FULL_SCALE = 5500;     /* hw_speaker.c */
  const CAPTION_FONTS = [16, 14, 12];   /* ui_speech_bubble.c, largest first */

  /* ---------------------------------------------------------------------- */
  /* Screen: one 320x240 panel = face + sensor chip + gear + speech bubble.  */
  /* ---------------------------------------------------------------------- */
  class Screen {
    constructor(canvas, opts) {
      this.canvas = canvas;
      this.ctx = canvas.getContext("2d");
      this.anim = new F.Animator();
      this.opts = Object.assign({ chrome: true }, opts);
      this.bubble = null;              /* {text, source, until, w, h, lines, font} */
      this.chip = null;                /* {temp, hum} strings or null */
      this.brightness = 1;
    }

    caption(source, text, ms) {
      const b = this.layoutBubble(text);
      b.source = source;
      b.until = ms ? performance.now() + ms : Infinity;
      this.bubble = b;
      const lift = Math.min(Math.trunc(b.h / LIFT_DIVISOR), LIFT_MAX);
      this.anim.setVerticalShift(-lift, SLIDE_MS);
    }

    hideCaption() {
      this.bubble = null;
      this.anim.setVerticalShift(0, SLIDE_MS);
    }

    layoutBubble(text) {
      const ctx = this.ctx;
      const maxText = BUBBLE_MAX_W - 2 * BUBBLE_PAD;
      const maxH = G.BUDDY_SCREEN_HEIGHT - 2 * EDGE;
      let best = null;
      for (const size of CAPTION_FONTS) {
        ctx.font = `500 ${size}px Montserrat, Inter, sans-serif`;
        const words = text.split(/\s+/);
        const lines = [];
        let line = "";
        for (const w of words) {
          const trial = line ? line + " " + w : w;
          if (ctx.measureText(trial).width > maxText && line) { lines.push(line); line = w; } else line = trial;
        }
        if (line) lines.push(line);
        const lineH = Math.round(size * 1.2);
        const textW = Math.min(maxText, Math.max(...lines.map((l) => ctx.measureText(l).width)));
        const w = Math.max(BUBBLE_MIN_W, Math.ceil(textW) + 2 * BUBBLE_PAD);
        const h = lines.length * lineH + 2 * BUBBLE_PAD;
        best = { text, lines, size, lineH, w, h };
        if (h <= maxH) break;
      }
      return best;
    }

    draw(now) {
      const ctx = this.ctx;
      const sx = this.canvas.width / G.BUDDY_SCREEN_WIDTH;
      const sy = this.canvas.height / G.BUDDY_SCREEN_HEIGHT;
      ctx.setTransform(sx, 0, 0, sy, 0, 0);
      ctx.fillStyle = T.background;
      ctx.fillRect(0, 0, G.BUDDY_SCREEN_WIDTH, G.BUDDY_SCREEN_HEIGHT);

      if (this.bubble && now > this.bubble.until) this.hideCaption();
      const pose = this.anim.tick(now);
      F.drawFace(ctx, pose, this.anim.shift);
      if (this.opts.chrome) {
        this.drawGear(ctx);
        if (this.chip) this.drawChip(ctx);
        if (this.bubble) this.drawBubble(ctx);
      }
      if (this.brightness < 1) {
        ctx.fillStyle = `rgba(0,0,0,${Math.pow(1 - this.brightness, 1.5) * 0.9})`;
        ctx.fillRect(0, 0, G.BUDDY_SCREEN_WIDTH, G.BUDDY_SCREEN_HEIGHT);
      }
    }

    pill(ctx, x, y, w, h, r, fill, stroke) {
      ctx.beginPath();
      ctx.roundRect(x, y, w, h, r);
      ctx.fillStyle = fill;
      ctx.fill();
      if (stroke) { ctx.strokeStyle = stroke; ctx.lineWidth = 1; ctx.stroke(); }
    }

    drawGear(ctx) {
      const x = EDGE, y = G.BUDDY_SCREEN_HEIGHT - EDGE - GEAR;
      this.pill(ctx, x, y, GEAR, GEAR, GEAR / 2, T.surface, T.surface_border);
      const cx = x + GEAR / 2, cy = y + GEAR / 2;
      ctx.save();
      ctx.translate(cx, cy);
      ctx.fillStyle = T.text_muted;
      for (let i = 0; i < 8; i++) {
        ctx.rotate(Math.PI / 4);
        ctx.fillRect(-2.2, -9.5, 4.4, 5);
      }
      ctx.beginPath(); ctx.arc(0, 0, 7, 0, Math.PI * 2); ctx.fill();
      ctx.fillStyle = T.surface;
      ctx.beginPath(); ctx.arc(0, 0, 3, 0, Math.PI * 2); ctx.fill();
      ctx.restore();
    }

    drawChip(ctx) {
      ctx.font = "600 14px Montserrat, Inter, sans-serif";
      const a = this.chip.temp, b = this.chip.hum;
      const wa = ctx.measureText(a).width, wb = ctx.measureText(b).width;
      const w = Math.ceil(wa + 8 + wb + 18), h = 25;
      const x = G.BUDDY_SCREEN_WIDTH - EDGE - w, y = EDGE;
      this.pill(ctx, x, y, w, h, 12, T.surface);
      ctx.textBaseline = "middle";
      ctx.fillStyle = T.espressif_red;
      ctx.fillText(a, x + 9, y + h / 2 + 1);
      ctx.fillStyle = T.text_muted;
      ctx.fillText(b, x + 9 + wa + 8, y + h / 2 + 1);
    }

    drawBubble(ctx) {
      const b = this.bubble;
      const x = G.BUDDY_SCREEN_WIDTH - EDGE - b.w;
      const y = G.BUDDY_SCREEN_HEIGHT - EDGE - b.h;
      this.pill(ctx, x, y, b.w, b.h, 16, T.surface, T.surface_border);
      ctx.font = `500 ${b.size}px Montserrat, Inter, sans-serif`;
      ctx.textBaseline = "top";
      ctx.fillStyle = b.source === "buddy" ? T.espressif_red : b.source === "user" ? T.text : T.text_muted;
      b.lines.forEach((l, i) => ctx.fillText(l, x + BUBBLE_PAD, y + BUBBLE_PAD + i * b.lineH + 1));
    }
  }

  /* ---------------------------------------------------------------------- */
  /* Audio: WebAudio playback with hw_speaker.c's loudness meter.            */
  /* ---------------------------------------------------------------------- */
  const audio = {
    ctx: null, gain: null, analyser: null, level: 0, volume: 0.7, buf: null, cache: new Map(),
    ensure() {
      if (this.ctx) { if (this.ctx.state === "suspended") this.ctx.resume(); return true; }
      const AC = window.AudioContext || window.webkitAudioContext;
      if (!AC) return false;
      this.ctx = new AC();
      this.analyser = this.ctx.createAnalyser();
      this.analyser.fftSize = 1024;
      this.gain = this.ctx.createGain();
      this.gain.gain.value = this.volume;
      this.analyser.connect(this.gain).connect(this.ctx.destination);
      this.buf = new Float32Array(this.analyser.fftSize);
      setInterval(() => this.meter(), G.FACE_LIP_SYNC_REFRESH_MS);
      return true;
    },
    meter() {
      this.analyser.getFloatTimeDomainData(this.buf);
      let total = 0, n = 0;
      for (let i = 0; i < this.buf.length; i += 8) { total += Math.abs(this.buf[i]) * 32768; n++; }
      const level = Math.min(100, Math.trunc((total / n) * 100 / LOUDNESS_FULL_SCALE));
      this.level = level > this.level ? level : Math.trunc((this.level * 3 + level) / 4);
    },
    setVolume(v) { this.volume = v; if (this.gain) this.gain.gain.value = v; },
    async load(url) {
      if (this.cache.has(url)) return this.cache.get(url);
      const p = fetch(url).then((r) => { if (!r.ok) throw new Error(r.status); return r.arrayBuffer(); })
        .then((b) => this.ctx.decodeAudioData(b));
      this.cache.set(url, p);
      return p;
    },
    async play(url) {
      if (!this.ensure()) return null;
      if (this.ctx.state !== "running") {
        await Promise.race([this.ctx.resume(), new Promise((r) => setTimeout(r, 300))]);
        if (this.ctx.state !== "running") { log("audio: browser blocked playback until you click the page"); return null; }
      }
      try {
        const buffer = await this.load(url);
        const src = this.ctx.createBufferSource();
        src.buffer = buffer;
        src.connect(this.analyser);
        src.start();
        return new Promise((res) => { src.onended = () => res(buffer.duration); });
      } catch (e) {
        log("audio unavailable for " + url + " (" + e.message + ")");
        return null;
      }
    },
  };

  /* ---------------------------------------------------------------------- */
  /* Log                                                                     */
  /* ---------------------------------------------------------------------- */
  const t0 = performance.now();
  function log(msg, cls) {
    const el = $("#log");
    if (!el) return;
    const line = document.createElement("div");
    const ms = Math.round(performance.now() - t0);
    line.innerHTML = `<span class="t">I (${ms})</span> <span class="sim">SIM</span> <span class="${cls || ""}"></span>`;
    line.lastElementChild.textContent = msg;
    el.appendChild(line);
    el.scrollTop = el.scrollHeight;
  }

  /* ---------------------------------------------------------------------- */
  /* Settings state: factory defaults from buddy_settings_reset_to_defaults() */
  /* ---------------------------------------------------------------------- */
  const settings = {
    volume: 70, chimes: true, brightness: 90, idleAntics: true, touchAntics: true,
    showReadout: true, fahrenheit: true, cloud: false, mode: 1, limit: G.BUDDY_DEFAULT_REQUESTS_PER_HOUR,
  };
  const sensors = { temp: 22.5, hum: 44, dock: true };

  /* ---------------------------------------------------------------------- */
  /* The main simulated device                                               */
  /* ---------------------------------------------------------------------- */
  const screenCanvas = $("#screen-canvas");
  const dpr = Math.min(window.devicePixelRatio || 1, 2);
  screenCanvas.width = 320 * 2 * dpr;
  screenCanvas.height = 240 * 2 * dpr;
  const device = new Screen(screenCanvas);
  device.anim.levelSource = () => audio.level;
  device.anim.on((type, d) => {
    if (type === "antic") log(`face_animator: antic "${d}"`);
    if (type === "emotion") log(`face_animator: emotion -> ${d}`);
  });

  let state = "idle";
  let muted = false;
  let listenTimer = null;
  let busy = false;

  function setState(s) {
    state = s;
    const label = { idle: "Idle", listening: "Listening", recognizing: "Recognizing", speaking: "Speaking",
      muted: "Muted", thinking: "Thinking" }[s] || s;
    $("#state-text").textContent = label;
    $("#state-badge").classList.toggle("live", s === "listening" || s === "speaking" || s === "recognizing");
  }

  function updateChip() {
    if (!sensors.dock || !settings.showReadout) { device.chip = null; return; }
    const t = settings.fahrenheit ? Math.trunc(sensors.temp * 9 / 5 + 32 + 0.5) + "°F"
      : Math.trunc(sensors.temp + 0.5) + "°C";
    device.chip = { temp: t, hum: Math.trunc(sensors.hum + 0.5) + "%" };
  }

  function chime(name) {
    if (!settings.chimes) return Promise.resolve();
    return audio.play(`audio/chime_${name}.wav`);
  }

  /* voice_assistant.c: handle_wake_word() */
  function wake() {
    audio.ensure();
    if (muted) {
      log("hw_microphone: muted, capture returns silence; WakeNet never hears the wake word");
      flashBadge();
      return false;
    }
    if (busy) return false;
    log(`wakeword: "${FW.firmware.wake_word}" detected`, "ok");
    setState("listening");
    device.anim.setEmotion("listening");
    device.caption("status", "Listening...", LISTENING_CAPTION_MS);
    chime("wake");
    clearTimeout(listenTimer);
    listenTimer = setTimeout(listenTimeout, G.BUDDY_LISTEN_WINDOW_MS);
    return true;
  }

  /* No phrase within the window: VOICE_ROUTE_NOTHING_HEARD. */
  function listenTimeout() {
    if (state !== "listening") return;
    log("assistant: nothing to act on (no phrase in the listen window)");
    device.anim.setEmotion("confused");
    device.caption("status", "I did not hear anything.", 2500);
    setTimeout(() => { device.anim.setEmotion("neutral"); setState(muted ? "muted" : "idle"); }, 2600);
  }

  function flashBadge() {
    const b = $("#state-badge");
    b.animate([{ transform: "translateX(0)" }, { transform: "translateX(-6px)" }, { transform: "translateX(6px)" },
      { transform: "translateX(0)" }], { duration: 320 });
  }

  /* voice_assistant.c: handle_recognised_command() + deliver_reply() */
  async function say(cmd) {
    audio.ensure();
    if (muted) { wake(); return; }
    if (busy) return;
    if (state !== "listening") {
      if (!wake()) return;
      await wait(900);
    }
    busy = true;
    clearTimeout(listenTimer);
    setState("recognizing");
    log(`multinet: command "${cmd.phrase}" (action ${cmd.action})`, "ok");
    device.caption("user", cmd.phrase, 2500);
    await wait(650);

    const reply = FW.replies[cmd.action];
    if (!reply || reply.runtime) {
      device.anim.setEmotion("thinking");
      device.caption("status", "On the device this answer is built at runtime. The simulator does not invent it.", 4200);
      log(`local_brain: ${cmd.action} is answered at runtime on the device; not simulated`);
      await wait(4300);
      device.anim.setEmotion("neutral");
      setState(muted ? "muted" : "idle");
      busy = false;
      return;
    }

    const line = reply.lines[Math.floor(Math.random() * reply.lines.length)];
    device.anim.setEmotion(reply.emotion);
    chime("understood");
    log(`local_brain: "${line.text}" [${reply.emotion}]`);
    if (reply.antic) device.anim.playAntic(Math.floor(Math.random() * FW.antics.length));

    if (line.clip) {
      await wait(350);
      device.caption("buddy", line.text, 600000);
      setState("speaking");
      device.anim.setSpeaking(true);
      log(`audio_voice: /spiffs/voice/${line.clip}.mp3`);
      const played = await audio.play(`voice/${line.clip}.mp3`);
      if (played === null) await wait(Math.max(1500, line.text.length * 60));
      device.anim.setSpeaking(false);
      device.caption("buddy", line.text, 1200);
    } else {
      device.caption("buddy", line.text, CAPTION_LINGER_MS);
      await wait(CAPTION_LINGER_MS);
    }
    setState(muted ? "muted" : "idle");
    busy = false;
  }

  const wait = (ms) => new Promise((r) => setTimeout(r, ms));

  /* Touch: the gear opens settings, anywhere else gets a random antic. */
  function onScreenTap(ev) {
    audio.ensure();
    const r = screenCanvas.getBoundingClientRect();
    const x = (ev.clientX - r.left) / r.width * 320;
    const y = (ev.clientY - r.top) / r.height * 240;
    if (ev.clientX !== undefined && x < EDGE + GEAR + 4 && y > 240 - EDGE - GEAR - 4) { openSettings(); return; }
    if (!settings.touchAntics) return;
    log("touch: poke");
    device.anim.playAntic(Math.floor(Math.random() * FW.antics.length));
  }

  /* ---------------------------------------------------------------------- */
  /* Settings screen (ui_settings.c layout, rendered at 320x240 and scaled)  */
  /* ---------------------------------------------------------------------- */
  const lv = $("#lv-settings");
  function sw(id, on, label) {
    return `<button class="lv-switch" role="switch" aria-checked="${on}" data-sw="${id}" aria-label="${label}"></button>`;
  }
  function buildSettings() {
    const tracks = 4;
    lv.innerHTML = `
      <div class="lv-head"><button class="lv-back" aria-label="Back">&#10094;</button>Settings</div>
      <div class="lv-body">
        <div class="lv-section">START HERE</div>
        <div class="lv-row">Question examples<span class="v">What to say</span></div>
        <div class="lv-row">Calculator<span class="v">On device only</span></div>
        <div class="lv-row">Wi-Fi<span class="v">Not connected (sim)</span></div>
        <div class="lv-note">Wi-Fi is only needed for the clock. Every phrase in the examples list, the faces, the timer, the sensors and the music all work with no network at all.</div>
        <div class="lv-section">SOUND</div>
        <div class="lv-row">Volume<input type="range" min="0" max="100" value="${settings.volume}" data-range="volume" aria-label="Volume"></div>
        <div class="lv-row">Chimes${sw("chimes", settings.chimes, "Chimes")}</div>
        <div class="lv-section">FACE</div>
        <div class="lv-row">Brightness<input type="range" min="10" max="100" value="${settings.brightness}" data-range="brightness" aria-label="Brightness"></div>
        <div class="lv-row">Idle antics${sw("idleAntics", settings.idleAntics, "Idle antics")}</div>
        <div class="lv-row">React to touch${sw("touchAntics", settings.touchAntics, "React to touch")}</div>
        <div class="lv-row" data-nav="faces" style="cursor:pointer">Face expressions<span class="v">Tap to try</span></div>
        <div class="lv-section">SENSORS</div>
        <div class="lv-row">Show readout${sw("showReadout", settings.showReadout, "Show readout")}</div>
        <div class="lv-row">Fahrenheit${sw("fahrenheit", settings.fahrenheit, "Fahrenheit")}</div>
        <div class="lv-section">VOICE</div>
        <div class="lv-row">Language<select aria-label="Language"><option>English (Hi E.S.P.)</option><option disabled>Chinese (Hi Lexin) - device only</option></select></div>
        <div class="lv-row">Room noise<select aria-label="Room noise"><option>Automatic</option><option>Quiet room</option><option>Noisy room</option></select></div>
        <div class="lv-section">ONLINE BRAIN (OPTIONAL)</div>
        <div class="lv-row">Answer any question${sw("cloud", settings.cloud, "Answer any question")}</div>
        <div class="lv-row">When to ask<select data-sel="mode" aria-label="When to ask"><option value="0">Never</option><option value="1">Only when stuck</option><option value="2">Always</option></select></div>
        <div class="lv-row">Limit per hour<select data-sel="limit" aria-label="Limit per hour"><option>10</option><option selected>30</option><option>60</option><option>120</option><option value="0">No limit</option></select></div>
        <div class="lv-row">API key<span class="v">Not set &middot; never stored here</span></div>
        <div class="lv-note">Off by default. Switching this on sends questions it does not recognise to OpenAI, which bills your account per question. In this simulator nothing is sent anywhere.</div>
        <div class="lv-section">MUSIC</div>
        <div class="lv-row">Source<select aria-label="Music source"><option>Flash</option><option>Wi-Fi stream</option></select></div>
        <div class="lv-row">On flash<span class="v">${tracks} track(s) on flash</span></div>
        <div class="lv-section">ABOUT</div>
        <div class="lv-row">${FW.firmware.name} ${FW.firmware.version}<span class="v">IP: offline (sim)</span></div>
        <div class="lv-row">Run diagnostics<span class="v">On device only</span></div>
        <div class="lv-row">Hearing<span class="v">On device only</span></div>
        <div class="lv-row">Factory reset<span class="v">On device only</span></div>
      </div>`;
    $("[data-sel=mode]", lv).value = String(settings.mode);
    $(".lv-back", lv).addEventListener("click", closeSettings);
    $$("[data-sw]", lv).forEach((b) => b.addEventListener("click", () => {
      const k = b.dataset.sw;
      settings[k] = !settings[k];
      b.setAttribute("aria-checked", settings[k]);
      applySettings();
      log(`settings: ${k} = ${settings[k]}`);
    }));
    $$("[data-range]", lv).forEach((r) => r.addEventListener("input", () => {
      settings[r.dataset.range] = +r.value;
      applySettings();
    }));
    $$("[data-sel]", lv).forEach((s) => s.addEventListener("change", () => {
      settings[s.dataset.sel] = +s.value;
      renderRouter();
    }));
    $("[data-nav=faces]", lv).addEventListener("click", () => { closeSettings(); selectTab("t-faces"); });
  }
  function scaleSettings() {
    const w = $("#screen").clientWidth;
    lv.style.transform = `scale(${w / 320})`;
  }
  function openSettings() {
    buildSettings();
    scaleSettings();
    lv.classList.add("open");
    $(".lv-back", lv).focus();
    log("ui_settings: open");
  }
  function closeSettings() { lv.classList.remove("open"); $("#screen").focus(); }
  function applySettings() {
    audio.setVolume(settings.volume / 100);
    device.brightness = settings.brightness / 100;
    device.anim.idleAntics = settings.idleAntics;
    updateChip();
    renderRouter();
  }

  /* ---------------------------------------------------------------------- */
  /* voice_router_decide(), ported line for line                             */
  /* ---------------------------------------------------------------------- */
  const R = { MIN_CONF: 0.10, NEARLY: 0.05, MIN_Q_S: 0.7, PEAK: 500, MIN_SPEECH: 0.35, SMART_SPEECH: 0.55 };
  function routerDecide(i) {
    if (i.matched && i.confidence >= R.MIN_CONF) return ["ANSWER_OFFLINE", "A phrase matched confidently; a recognised command is never sent up."];
    if (!(i.recorded >= R.MIN_Q_S) || !(i.peak >= R.PEAK)) return ["NOTHING_HEARD", "Too short or too quiet to be a question."];
    if (i.mode === 0) return ["ANSWER_OFFLINE", "Mode is Never: everything stays on the chip."];
    if (!i.cloudReady) {
      if (i.mode === 1) return ["ANSWER_OFFLINE", "Cloud not ready; Only when stuck falls back to offline."];
      return ["DECLINE_NO_CLOUD", "Would ask, but the online brain is off or has no key."];
    }
    if (i.speech < R.MIN_SPEECH) return ["ANSWER_OFFLINE", "Barely any speech in the recording."];
    if (i.mode === 1) {
      if (i.matched && i.confidence >= R.NEARLY) return ["ANSWER_OFFLINE", "Nearly matched a phrase: probably a command said badly."];
      if (i.speech < R.SMART_SPEECH) return ["ANSWER_OFFLINE", "Not a sentence's worth of speech."];
    }
    if (i.allowed > 0 && i.used >= i.allowed) return ["DECLINE_BUDGET", "Hourly allowance spent; answers offline until the hour turns."];
    return ["ASK_CLOUD", "Worth one request."];
  }
  const routerInputs = { matched: false, confidence: 0.03, recorded: 2.4, speech: 1.6, peak: 2400, cloudReady: true, used: 3 };
  function renderRouter() {
    const el = $("#router");
    if (!el) return;
    if (!el.dataset.built) {
      el.dataset.built = "1";
      el.innerHTML = `
        <div class="field"><label>When to ask <span class="tag-sim">sim</span></label><output id="r-mode"></output>
          <input type="range" min="0" max="2" step="1" data-r="mode"><small>Never &middot; Only when stuck &middot; Always (also in the on-screen Settings)</small></div>
        <div class="field"><label>Recogniser confidence</label><output id="r-conf"></output><input type="range" min="0" max="0.3" step="0.01" data-r="confidence"></div>
        <div class="field"><label>Seconds recorded</label><output id="r-rec"></output><input type="range" min="0" max="10" step="0.1" data-r="recorded"></div>
        <div class="field"><label>Seconds of speech</label><output id="r-sp"></output><input type="range" min="0" max="10" step="0.05" data-r="speech"></div>
        <div class="field"><label>Peak amplitude</label><output id="r-pk"></output><input type="range" min="0" max="8000" step="50" data-r="peak"></div>
        <div class="field"><label>Cloud ready (switch on + key)</label><button class="lv-switch" role="switch" data-rs="cloudReady" aria-label="Cloud ready"></button></div>
        <div class="field"><label>Requests used this hour</label><output id="r-used"></output><input type="range" min="0" max="130" step="1" data-r="used"></div>
        <div class="verdict" id="r-verdict"></div>`;
      $$("[data-r]", el).forEach((r) => {
        r.value = r.dataset.r === "mode" ? settings.mode : routerInputs[r.dataset.r];
        r.addEventListener("input", () => {
          if (r.dataset.r === "mode") settings.mode = +r.value; else routerInputs[r.dataset.r] = +r.value;
          renderRouter();
        });
      });
      $("[data-rs]", el).addEventListener("click", (e) => { routerInputs.cloudReady = !routerInputs.cloudReady; renderRouter(); });
    }
    const i = Object.assign({}, routerInputs, { mode: settings.mode, allowed: settings.limit,
      matched: routerInputs.confidence > 0 });
    $("#r-mode").textContent = ["Never", "Only when stuck", "Always"][settings.mode];
    $("[data-r=mode]").value = settings.mode;
    $("#r-conf").textContent = i.confidence.toFixed(2);
    $("#r-rec").textContent = i.recorded.toFixed(1) + " s";
    $("#r-sp").textContent = i.speech.toFixed(2) + " s";
    $("#r-pk").textContent = i.peak;
    $("#r-used").textContent = i.used + " / " + (i.allowed || "no limit");
    $("[data-rs]").setAttribute("aria-checked", i.cloudReady);
    const [route, why] = routerDecide(i);
    $("#r-verdict").innerHTML = `<b>VOICE_ROUTE_${route}</b><br>${why}`;
  }

  /* ---------------------------------------------------------------------- */
  /* Panels                                                                  */
  /* ---------------------------------------------------------------------- */
  function selectTab(id) {
    $$(".tab").forEach((t) => {
      const on = t.id === id;
      t.setAttribute("aria-selected", on);
      $("#" + t.getAttribute("aria-controls")).hidden = !on;
    });
  }

  function renderStatic(canvas, pose, scale) {
    const c = canvas.getContext("2d");
    c.setTransform(canvas.width / 320, 0, 0, canvas.height / 240, 0, 0);
    c.fillStyle = T.background;
    c.fillRect(0, 0, 320, 240);
    F.drawFace(c, pose, 0);
  }

  function buildPanels() {
    /* Phrases */
    const box = $("#phrases");
    const search = $("#phrase-search");
    search.placeholder = `Search ${FW.commands.length} offline phrases`;
    FW.commands.forEach((c) => {
      const b = document.createElement("button");
      const runtime = !FW.replies[c.action] || FW.replies[c.action].runtime;
      b.className = "phrase" + (runtime ? " runtime" : "");
      b.textContent = c.phrase;
      b.title = runtime ? "Answered at runtime on the device" : "Answered from a fixed reply with a recorded clip";
      b.addEventListener("click", () => say(c));
      box.appendChild(b);
    });
    search.addEventListener("input", () => {
      const q = search.value.trim().toLowerCase();
      $$(".phrase", box).forEach((b) => { b.hidden = q && !b.textContent.includes(q); });
    });

    /* Faces */
    const grid = $("#faces-grid");
    FW.emotions.forEach((e) => {
      const tile = document.createElement("button");
      tile.className = "face-tile";
      const cv = document.createElement("canvas");
      cv.width = 160; cv.height = 120;
      renderStatic(cv, e.pose);
      tile.append(cv, document.createTextNode(e.name));
      tile.addEventListener("click", () => {
        $$(".face-tile.on").forEach((t) => t.classList.remove("on"));
        tile.classList.add("on");
        device.anim.setEmotion(e.name);
      });
      tile.addEventListener("mouseenter", () => device.anim.flashEmotion(e.name, 600));
      grid.appendChild(tile);
    });
    const antics = $("#antics");
    FW.antics.forEach((a, i) => {
      const b = document.createElement("button");
      b.className = "phrase";
      b.textContent = a.name;
      b.addEventListener("click", () => device.anim.playAntic(i));
      antics.appendChild(b);
    });

    $$(".tab").forEach((t) => t.addEventListener("click", () => selectTab(t.id)));

    /* Sensors */
    const temp = $("#temp"), hum = $("#hum");
    const syncSensors = () => {
      sensors.temp = +temp.value; sensors.hum = +hum.value;
      $("#temp-out").textContent = sensors.temp.toFixed(1) + " °C";
      $("#hum-out").textContent = sensors.hum + " %";
      updateChip();
    };
    temp.addEventListener("input", syncSensors);
    hum.addEventListener("input", syncSensors);
    syncSensors();
    $("#dock").addEventListener("click", (e) => {
      sensors.dock = !sensors.dock;
      e.currentTarget.setAttribute("aria-checked", sensors.dock);
      updateChip();
      log(`sensors: dock ${sensors.dock ? "present" : "absent"} (simulated)`);
    });
    $("#arrive").addEventListener("click", () => {
      if (!sensors.dock) { log("sensors: no dock, so no radar"); return; }
      log("presence: someone arrived (simulated radar event)");
      device.anim.flashEmotion("curious", 2500);
    });

    /* Device controls */
    $("#wake").addEventListener("click", wake);
    $("#mute").addEventListener("click", (e) => {
      muted = !muted;
      e.currentTarget.setAttribute("aria-pressed", muted);
      log(`buttons: mute switch ${muted ? "MUTED" : "live"}`);
      if (muted) { clearTimeout(listenTimer); if (state === "listening") { device.hideCaption(); device.anim.setEmotion("neutral"); } }
      setState(muted ? "muted" : "idle");
    });
    const scr = $("#screen");
    screenCanvas.addEventListener("click", onScreenTap);
    scr.addEventListener("keydown", (e) => {
      if (e.target !== scr) return;
      if (e.key === "Enter" || e.key === " ") { e.preventDefault(); onScreenTap({}); }
      if (e.key.toLowerCase() === "s") openSettings();
    });
    window.addEventListener("resize", scaleSettings);
    document.addEventListener("keydown", (e) => { if (e.key === "Escape" && lv.classList.contains("open")) closeSettings(); });
    renderRouter();
  }

  /* ---------------------------------------------------------------------- */
  /* Page choreography: morph section, pipeline cards, stats, reveals        */
  /* ---------------------------------------------------------------------- */
  const MORPH = ["neutral", "happy", "curious", "surprised", "wink", "love", "sleepy", "starry",
    "sceptical", "giggly", "determined", "shocked", "cosy", "playful", "triumphant", "neutral"];
  const FIELDS = ["eye_width", "eye_height", "eye_corner_radius", "eye_lower_lid", "eye_upper_lid",
    "brow_offset_y", "brow_inner_tilt", "brow_spread", "brow_asymmetry", "mouth_width", "mouth_curve",
    "mouth_open_height", "mouth_offset_y", "blush_opacity", "look_x", "look_y", "face_offset_y", "face_offset_x"];

  function setupMorph() {
    const sec = $("#faces");
    const cv = $("#morph-canvas");
    cv.width = 320 * 2 * dpr; cv.height = 240 * 2 * dpr;
    const ctx = cv.getContext("2d");
    const table = $("#pose-table");
    table.innerHTML = FIELDS.map((f) => `<span>${f.replace(/_/g, " ")} <b data-f="${f}">0</b></span>`).join("");
    const cells = {};
    $$("b[data-f]", table).forEach((b) => { cells[b.dataset.f] = b; });
    let last = null, lastName = "";
    function frame() {
      const r = sec.getBoundingClientRect();
      const total = sec.offsetHeight - window.innerHeight;
      const p = Math.min(1, Math.max(0, -r.top / total));
      const seg = p * (MORPH.length - 1);
      const i = Math.min(MORPH.length - 2, Math.floor(seg));
      const f = seg - i;
      const ease = f < 0.35 ? 0 : f > 0.65 ? 1 : (f - 0.35) / 0.3;
      const k = 0.5 - Math.cos(ease * Math.PI) / 2;
      const pose = F.blendPose(F.POSES[MORPH[i]], F.POSES[MORPH[i + 1]], Math.round(k * 255));
      ctx.setTransform(cv.width / 320, 0, 0, cv.height / 240, 0, 0);
      ctx.fillStyle = T.background; ctx.fillRect(0, 0, 320, 240);
      F.drawFace(ctx, pose, 0);
      const name = k < 0.5 ? MORPH[i] : MORPH[i + 1];
      if (name !== lastName) { $("#morph-name").textContent = name; lastName = name; }
      FIELDS.forEach((fk) => {
        const v = pose[fk];
        if (!last || last[fk] !== v) { cells[fk].textContent = v; cells[fk].classList.add("hot"); }
        else cells[fk].classList.remove("hot");
      });
      last = pose;
      $("#morph-progress").style.width = (p * 100).toFixed(1) + "%";
    }
    return frame;
  }

  const STAGES = [
    { id: "idle", title: "Idle", text: "Blinks every 2.6 to 6.4 s, fidgets every 7 to 17 s.", src: "face_animator.c", emotion: "neutral" },
    { id: "wake", title: "Wake", text: `"${FW.firmware.wake_word}" heard on the chip. Chirp, ears up.`, src: "handle_wake_word()", emotion: "listening" },
    { id: "match", title: "Understood", text: "MultiNet matched an offline phrase.", src: "handle_recognised_command()", emotion: "happy" },
    { id: "think", title: "Thinking", text: "Only for unmatched questions, only if allowed.", src: "voice_router_decide()", emotion: "thinking" },
    { id: "speak", title: "Speaking", text: "Mouth follows the speaker's loudness meter.", src: "lip_sync_timer_cb()", emotion: "speaking", open: 22 },
  ];

  function setupPipeline() {
    const el = $("#pipeline");
    STAGES.forEach((s, idx) => {
      const card = document.createElement("button");
      card.className = "stage-card reveal";
      card.style.transitionDelay = idx * 80 + "ms";
      const cv = document.createElement("canvas");
      cv.width = 320; cv.height = 240;
      const pose = Object.assign({}, F.POSES[s.emotion]);
      if (s.open) pose.mouth_open_height = s.open;
      renderStatic(cv, pose);
      card.innerHTML = `<h3></h3><p></p><span class="src"></span>`;
      card.prepend(cv);
      $("h3", card).textContent = s.title;
      $("p", card).textContent = s.text;
      $(".src", card).textContent = s.src;
      card.addEventListener("click", () => runStage(s.id));
      el.appendChild(card);
    });
    let i = 0;
    setInterval(() => {
      $$(".stage-card", el).forEach((c, j) => c.classList.toggle("active", j === i));
      i = (i + 1) % STAGES.length;
    }, 1600);
  }

  function runStage(id) {
    $("#simulator").scrollIntoView({ behavior: reduceMotion ? "auto" : "smooth" });
    const pick = (action) => FW.commands.find((c) => c.action === action);
    setTimeout(() => {
      if (id === "idle") { device.anim.setEmotion("neutral"); device.anim.playAntic(0); }
      if (id === "wake") wake();
      if (id === "match" || id === "speak") say(pick("tell_joke"));
      if (id === "think") { selectTab("t-brain"); device.anim.flashEmotion("thinking", 2500); log("router: see the Online brain tab; nothing is sent from this page"); }
    }, reduceMotion ? 0 : 700);
  }

  function setupStats() {
    const clips = FW.voice_clip_count;
    const stats = [
      [FW.commands.length, "offline English phrases", "voice_commands.c"],
      [FW.emotions.length, "expressions, zero bitmaps", "face_emotions.c"],
      [clips, "recorded voice clips on flash", "spiffs/voice/*.mp3"],
      [0, "bytes sent anywhere by default", "online brain ships off"],
    ];
    $("#stats").innerHTML = stats.map(([n, l, s]) =>
      `<div class="stat reveal"><b data-to="${n}">0</b><span>${l}</span><small>${s}</small></div>`).join("");
    $$("[data-count]").forEach((el) => {
      const k = el.dataset.count;
      el.textContent = k === "clips" ? clips : FW[k].length;
    });
    $$("[data-fw=version]").forEach((el) => { el.textContent = "v" + FW.firmware.version; });
    $$("[data-fw=sensor-refresh]").forEach((el) => { el.textContent = G.BUDDY_SENSOR_REFRESH_MS / 1000 + " s"; });
  }

  function countUp(el) {
    const to = +el.dataset.to;
    if (reduceMotion || to === 0) { el.textContent = to; return; }
    const start = performance.now();
    const step = (now) => {
      const t = Math.min(1, (now - start) / 1400);
      el.textContent = Math.round(to * (1 - Math.pow(1 - t, 3)));
      if (t < 1) requestAnimationFrame(step);
    };
    requestAnimationFrame(step);
  }

  function setupReveal() {
    const io = new IntersectionObserver((entries) => entries.forEach((e) => {
      if (!e.isIntersecting) return;
      e.target.classList.add("in");
      $$("[data-to]", e.target).forEach(countUp);
      if (e.target.matches("[data-to]")) countUp(e.target);
      io.unobserve(e.target);
    }), { threshold: 0.18 });
    $$(".reveal").forEach((el) => io.observe(el));

    const nav = $("#nav");
    const night = $$(".night");
    const navCheck = () => {
      const y = 26;
      nav.classList.toggle("dark", night.some((n) => { const r = n.getBoundingClientRect(); return r.top < y && r.bottom > y; }));
    };
    window.addEventListener("scroll", navCheck, { passive: true });
    navCheck();
  }

  /*
   * The hero's screen: the full UI (gear, captions) on the 3D device, replaying
   * real exchanges from the reply table on a loop. No sensor chip, so nothing
   * on the landing page looks like a reading. Silent: audio only plays in the
   * simulator, after the visitor asks for it.
   */
  const heroCanvas = document.createElement("canvas");
  heroCanvas.width = 960; heroCanvas.height = 720;
  const hero = new Screen(heroCanvas, { chrome: true });
  const DEMO = ["hello buddy", "tell me a joke", "give me a wink", "how are you doing", "tell me a fact",
    "look totally amazed", "read my fortune", "act all shy now", "celebrate with me"];
  let demoIndex = 0;
  async function heroDemo() {
    for (;;) {
      await wait(2600);
      if (document.hidden || hero.anim.antic) continue;
      const cmd = FW.commands.find((c) => c.phrase === DEMO[demoIndex++ % DEMO.length]);
      const reply = cmd && FW.replies[cmd.action];
      if (!reply || reply.runtime) continue;
      hero.anim.setEmotion("listening");
      hero.caption("status", "Listening...", 1300);
      await wait(1400);
      hero.caption("user", cmd.phrase, 1500);
      await wait(1500);
      const line = reply.lines[Math.floor(Math.random() * reply.lines.length)];
      hero.anim.setEmotion(reply.emotion);
      hero.caption("buddy", line.text, Math.max(2600, line.text.length * 55));
      await wait(Math.max(2600, line.text.length * 55) + 300);
      hero.anim.setEmotion("neutral");
      await wait(2200);
    }
  }
  if (!reduceMotion) heroDemo();

  /* ---------------------------------------------------------------------- */
  /* Boot                                                                    */
  /* ---------------------------------------------------------------------- */
  function boot() {
    setupStats();
    buildPanels();
    setupPipeline();
    const morphFrame = setupMorph();
    setupReveal();
    applySettings();
    log(`${FW.firmware.name} ${FW.firmware.version} - browser simulation started`);
    log(`${FW.emotions.length} expressions, ${FW.antics.length} antics, ${FW.commands.length} phrases loaded from firmware-data.js`);

    const fallback = $("#hero-fallback");
    const fctx = fallback.getContext("2d");
    function loop(now) {
      device.draw(now);
      hero.draw(now);
      morphFrame();
      if ($("#hero-stage").classList.contains("no-gl")) {
        fctx.clearRect(0, 0, fallback.width, fallback.height);
        fctx.drawImage(heroCanvas, 0, 0, fallback.width, fallback.height);
      }
      requestAnimationFrame(loop);
    }
    requestAnimationFrame(loop);
  }

  window.DeskBuddySim = { heroCanvas, hero, device, say, wake };
  if (document.fonts && document.fonts.ready) document.fonts.ready.then(boot); else boot();
})();
