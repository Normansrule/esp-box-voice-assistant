/*
 * face.js - the Desk Buddy face, ported from the firmware to <canvas>.
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 *
 * Every number comes from window.FIRMWARE, which tools/extract_firmware_data.py
 * generates from main/buddy_config.h, main/face/face_emotions.c and friends.
 * The layout functions below mirror main/face/face_view.c, the blend mirrors
 * main/face/face_pose.c and the Animator mirrors main/face/face_animator.c,
 * including C's truncating integer division, so a pose drawn here lands on the
 * same pixels as on the 320x240 panel.
 *
 * This is a SIMULATION of the screen. It never claims to be the device.
 */
(function () {
  "use strict";

  const FW = window.FIRMWARE;
  const G = FW.geometry;
  const T = FW.theme;
  const W = G.BUDDY_SCREEN_WIDTH;
  const H = G.BUDDY_SCREEN_HEIGHT;
  const CX = W / 2;

  const POSES = {};
  FW.emotions.forEach((e) => { POSES[e.name] = e.pose; });

  /* C semantics: int16 maths with division that truncates toward zero. */
  const div = (a, b) => Math.trunc(a / b);
  const clamp = (v, lo, hi) => (v < lo ? lo : v > hi ? hi : v);

  function blendPose(from, to, progress) {
    const p = clamp(Math.round(progress), 0, 255);
    const out = {};
    for (const key of Object.keys(to)) {
      const a = from[key], b = to[key];
      if (typeof b === "boolean") out[key] = p < 128 ? a : b;
      else out[key] = a + div((b - a) * p, 255);
    }
    out.blush_opacity = clamp(out.blush_opacity, 0, 255);
    return out;
  }

  function roundRect(ctx, x, y, w, h, r) {
    r = Math.max(0, Math.min(r, w / 2, h / 2));
    ctx.beginPath();
    ctx.moveTo(x + r, y);
    ctx.arcTo(x + w, y, x + w, y + h, r);
    ctx.arcTo(x + w, y + h, x, y + h, r);
    ctx.arcTo(x, y + h, x, y, r);
    ctx.arcTo(x, y, x + w, y, r);
    ctx.closePath();
  }

  /* face_view.c: layout_eyes() */
  function drawEyes(ctx, pose, fdx, fdy, ldx, ldy, colours) {
    for (let side = 0; side < 2; side++) {
      const shut = side === 0 ? pose.left_eye_shut : pose.right_eye_shut;
      const width = clamp(pose.eye_width, 8, 160);
      const height = shut ? G.EYE_SHUT_HEIGHT : clamp(pose.eye_height, 4, 200);
      let radius = shut ? div(G.EYE_SHUT_HEIGHT, 2) : pose.eye_corner_radius;
      let lower = shut ? G.EYE_SHUT_CRESCENT : pose.eye_lower_lid;
      let upper = shut ? 0 : pose.eye_upper_lid;
      radius = clamp(radius, 0, div(Math.min(width, height), 2));
      lower = clamp(lower, 0, height);
      upper = clamp(upper, 0, height);

      const spacing = side === 0 ? -G.FACE_EYE_SPACING_FROM_CENTRE : G.FACE_EYE_SPACING_FROM_CENTRE;
      const cx = CX + spacing + fdx + ldx;
      const cy = G.FACE_EYE_CENTRE_Y + fdy + ldy;
      const x = cx - div(width, 2);
      const y = cy - div(height, 2);

      ctx.save();
      roundRect(ctx, x, y, width, height, radius);
      ctx.clip();
      /* Lids are holes in the eye (even-odd fill), not background paint on
         top of it, so no red fringe survives along the cut. */
      const cutter = Math.max(width * 2, 48);
      const cutterX = x + div(width - cutter, 2) + cutter / 2;
      ctx.beginPath();
      ctx.rect(x - 1, y - 1, width + 2, height + 2);
      for (const top of [y + height - lower, y + upper - cutter]) {
        ctx.moveTo(cutterX + cutter / 2, top + cutter / 2);
        ctx.arc(cutterX, top + cutter / 2, cutter / 2, 0, Math.PI * 2);
      }
      ctx.fillStyle = colours.red;
      ctx.fill("evenodd");
      ctx.restore();
    }
  }

  /* face_view.c: layout_brows() */
  function drawBrows(ctx, pose, fdx, fdy, ldx, ldy, colours) {
    if (!pose.brows_visible) return;
    const tilt = clamp(pose.brow_inner_tilt, -16, 16);
    const halfPen = div(G.FACE_BROW_THICKNESS, 2);
    const farX = halfPen + G.FACE_BROW_LENGTH;
    const midX = halfPen + div(G.FACE_BROW_LENGTH, 2);
    const midY = G.BROW_BASELINE_Y + div(tilt, 2) - G.BROW_ARCH;
    const canvasW = G.FACE_BROW_LENGTH + G.FACE_BROW_THICKNESS;
    const pts = [
      [[halfPen, G.BROW_BASELINE_Y], [midX, midY], [farX, G.BROW_BASELINE_Y + tilt]],
      [[halfPen, G.BROW_BASELINE_Y + tilt], [midX, midY], [farX, G.BROW_BASELINE_Y]],
    ];
    let baseline = G.FACE_EYE_CENTRE_Y - div(G.FACE_EYE_HEIGHT, 2) - G.FACE_BROW_GAP_ABOVE_EYE
      + pose.brow_offset_y + fdy + div(ldy, 2);
    if (baseline < G.FACE_BROW_MIN_BASELINE_Y) baseline = G.FACE_BROW_MIN_BASELINE_Y;

    ctx.strokeStyle = colours.red;
    ctx.lineWidth = G.FACE_BROW_THICKNESS;
    ctx.lineCap = "round";
    ctx.lineJoin = "round";
    for (let side = 0; side < 2; side++) {
      const spacing = side === 0 ? -(G.FACE_EYE_SPACING_FROM_CENTRE + pose.brow_spread)
        : (G.FACE_EYE_SPACING_FROM_CENTRE + pose.brow_spread);
      const cx = CX + spacing + fdx + div(ldx, 2);
      const extra = side === 0 ? pose.brow_asymmetry : 0;
      const ox = cx - div(canvasW, 2);
      const oy = baseline - G.BROW_BASELINE_Y + extra;
      ctx.beginPath();
      pts[side].forEach(([px, py], i) => (i ? ctx.lineTo(ox + px, oy + py) : ctx.moveTo(ox + px, oy + py)));
      ctx.stroke();
    }
  }

  /* face_view.c: layout_mouth() */
  function drawMouth(ctx, pose, fdx, fdy, ldx, ldy, colours) {
    const width = clamp(pose.mouth_width, 16, G.MOUTH_CANVAS_WIDTH - 20);
    const curve = clamp(pose.mouth_curve, -34, 34);
    const open = clamp(pose.mouth_open_height, 0, G.FACE_MOUTH_MAX_OPEN_HEIGHT);
    const cx = CX + fdx + div(ldx, 3);
    const cy = G.FACE_MOUTH_CENTRE_Y + pose.mouth_offset_y + fdy + div(ldy, 3);
    ctx.fillStyle = colours.red;
    ctx.strokeStyle = colours.red;

    if (open > G.FACE_MOUTH_THICKNESS) {
      roundRect(ctx, cx - div(width, 2), cy + curve - div(open, 2), width, open, Math.min(width, open) / 2);
      ctx.fill();
      return;
    }
    const last = G.FACE_MOUTH_POINT_COUNT - 1;
    const ox = cx - div(G.MOUTH_CANVAS_WIDTH, 2);
    const oy = cy - G.MOUTH_BASELINE_Y;
    ctx.lineWidth = G.FACE_MOUTH_THICKNESS;
    ctx.lineCap = "round";
    ctx.lineJoin = "round";
    ctx.beginPath();
    for (let i = 0; i <= last; i++) {
      const u = 2 * i - last;
      const x = div(G.MOUTH_CANVAS_WIDTH, 2) - div(width, 2) + div(width * i, last);
      const y = G.MOUTH_BASELINE_Y + curve - div(curve * u * u, last * last);
      i ? ctx.lineTo(ox + x, oy + y) : ctx.moveTo(ox + x, oy + y);
    }
    ctx.stroke();
  }

  /* face_view.c: layout_blush() */
  function drawBlush(ctx, pose, fdx, fdy, ldx, ldy, colours) {
    if (pose.blush_opacity <= 0) return;
    ctx.save();
    ctx.globalAlpha = pose.blush_opacity / 255;
    ctx.fillStyle = colours.blush;
    const d = G.FACE_BLUSH_DIAMETER;
    for (let side = 0; side < 2; side++) {
      const spacing = side === 0 ? -G.FACE_BLUSH_OFFSET_X : G.FACE_BLUSH_OFFSET_X;
      const x = CX + spacing + fdx + div(ldx, 2) - div(d, 2);
      const y = G.FACE_BLUSH_OFFSET_Y + fdy + div(ldy, 2) - div(d, 2);
      ctx.beginPath();
      ctx.arc(x + d / 2, y + d / 2, d / 2, 0, Math.PI * 2);
      ctx.fill();
    }
    ctx.restore();
  }

  const DEFAULT_COLOURS = { red: T.espressif_red, bg: T.background, blush: T.blush };

  /** Draw one pose in 320x240 screen space. verticalShift = speech-bubble lift. */
  function drawFace(ctx, pose, verticalShift, colours) {
    colours = colours || DEFAULT_COLOURS;
    const fdx = pose.face_offset_x;
    const fdy = pose.face_offset_y + (verticalShift || 0);
    const ldx = div(clamp(pose.look_x, -100, 100) * G.FACE_EYE_LOOK_RANGE_X, 100);
    const ldy = div(clamp(pose.look_y, -100, 100) * G.FACE_EYE_LOOK_RANGE_Y, 100);
    drawBlush(ctx, pose, fdx, fdy, ldx, ldy, colours);
    drawEyes(ctx, pose, fdx, fdy, ldx, ldy, colours);
    drawBrows(ctx, pose, fdx, fdy, ldx, ldy, colours);
    drawMouth(ctx, pose, fdx, fdy, ldx, ldy, colours);
  }

  /* LVGL's lv_anim_path_ease_in_out is a cubic bezier (0, 0.4, 0.6, 1). */
  function bezier3(t, u0, u1, u2, u3) {
    const r = 1 - t;
    return r * r * r * u0 + 3 * r * r * t * u1 + 3 * r * t * t * u2 + t * t * t * u3;
  }
  const easeInOut = (t) => bezier3(t, 0, 0.4, 0.6, 1);
  const easeOut = (t) => bezier3(t, 0, 0.9, 1, 1);
  const rand = (lo, hi) => lo + Math.random() * (hi - lo);

  /**
   * Animator - mirrors face_animator.c: resting emotion, blink timer, idle
   * antics, flash, speaking with lip sync driven by a 0-100 output level.
   */
  class Animator {
    constructor() {
      this.current = { ...POSES.neutral };
      this.from = { ...POSES.neutral };
      this.to = { ...POSES.neutral };
      this.animStart = 0;
      this.animDur = 0;
      this.shift = 0;
      this.shiftFrom = 0;
      this.shiftTo = 0;
      this.shiftStart = 0;
      this.shiftDur = 0;
      this.resting = "neutral";
      this.speaking = false;
      this.idleAntics = true;
      this.antic = null;
      this.timers = new Set();
      this.levelSource = null;      /* () => 0..100, the speaker output level */
      this.lastActivity = performance.now();
      this.listeners = new Set();
      this.scheduleBlink();
      this.scheduleIdle();
      this.lipTimer = null;
    }

    on(fn) { this.listeners.add(fn); return () => this.listeners.delete(fn); }
    emit(type, detail) { this.listeners.forEach((fn) => fn(type, detail)); }

    later(fn, ms) {
      const id = setTimeout(() => { this.timers.delete(id); fn(); }, ms);
      this.timers.add(id);
      return id;
    }
    cancel(id) { if (id) { clearTimeout(id); this.timers.delete(id); } }

    animateTo(target, ms) {
      this.from = { ...this.current };
      this.to = { ...target };
      this.animStart = performance.now();
      this.animDur = ms;
      if (!ms) this.current = { ...target };
    }

    setVerticalShift(px, ms) {
      this.shiftFrom = this.shift;
      this.shiftTo = px;
      this.shiftStart = performance.now();
      this.shiftDur = ms || 0;
      if (!ms) this.shift = px;
    }

    tick(now) {
      if (this.animDur) {
        const t = Math.min(1, (now - this.animStart) / this.animDur);
        this.current = blendPose(this.from, this.to, Math.round(easeInOut(t) * 255));
        if (t >= 1) { this.animDur = 0; this.current = { ...this.to }; }
      }
      if (this.shiftDur) {
        const t = Math.min(1, (now - this.shiftStart) / this.shiftDur);
        this.shift = Math.round(this.shiftFrom + (this.shiftTo - this.shiftFrom) * easeOut(t));
        if (t >= 1) this.shiftDur = 0;
      }
      return this.current;
    }

    /* --- blink --------------------------------------------------------- */
    scheduleBlink() {
      this.cancel(this.blinkTimer);
      this.blinkTimer = this.later(() => {
        if (!this.antic) this.blinkNow();
        this.scheduleBlink();
      }, rand(G.FACE_BLINK_INTERVAL_MIN_MS, G.FACE_BLINK_INTERVAL_MAX_MS));
    }

    blinkNow() {
      const shut = { ...this.to };
      if (shut.left_eye_shut && shut.right_eye_shut) return;
      shut.left_eye_shut = true;
      shut.right_eye_shut = true;
      this.animateTo(shut, G.FACE_BLINK_CLOSE_MS);
      this.later(() => {
        const open = { ...this.to, left_eye_shut: false, right_eye_shut: false };
        this.animateTo(open, G.FACE_BLINK_OPEN_MS);
      }, G.FACE_BLINK_CLOSE_MS + 20);
    }

    /* --- antics -------------------------------------------------------- */
    scheduleIdle() {
      this.cancel(this.idleTimer);
      this.idleTimer = this.later(() => this.idleTick(),
        rand(G.FACE_IDLE_ANTIC_INTERVAL_MIN_MS, G.FACE_IDLE_ANTIC_INTERVAL_MAX_MS));
    }

    idleTick() {
      if (!this.idleAntics || this.speaking || this.antic) { this.scheduleIdle(); return; }
      this.playAntic(Math.floor(Math.random() * FW.antics.length));
    }

    cancelAntic() {
      this.cancel(this.anticTimer);
      this.antic = null;
    }

    playAntic(index) {
      const antic = FW.antics[index];
      if (!antic || this.speaking) return;
      this.cancelAntic();
      this.antic = { def: antic, step: 0 };
      this.emit("antic", antic.name);
      this.runAnticStep();
    }

    runAnticStep() {
      if (!this.antic) return;
      const { def } = this.antic;
      if (this.antic.step >= def.steps.length) {
        this.cancelAntic();
        this.returnToResting(G.FACE_EXPRESSION_TRANSITION_MS);
        this.scheduleIdle();
        return;
      }
      const s = def.steps[this.antic.step++];
      const pose = { ...POSES[s.emotion], look_x: s.look_x, look_y: s.look_y,
        face_offset_x: s.face_offset_x, face_offset_y: s.face_offset_y };
      this.animateTo(pose, s.transition_ms);
      this.anticTimer = this.later(() => this.runAnticStep(), Math.max(10, s.transition_ms + s.hold_ms));
    }

    /* --- emotions ------------------------------------------------------ */
    returnToResting(ms) { this.animateTo(POSES[this.resting], ms); }

    setEmotion(name, ms) {
      if (!POSES[name]) name = "neutral";
      this.cancelAntic();
      this.cancel(this.flashTimer);
      this.resting = name;
      this.lastActivity = performance.now();
      this.returnToResting(ms || G.FACE_EXPRESSION_TRANSITION_MS);
      this.emit("emotion", name);
    }

    flashEmotion(name, holdMs) {
      if (!POSES[name]) return;
      this.cancelAntic();
      this.cancel(this.flashTimer);
      this.animateTo(POSES[name], G.FACE_EXPRESSION_TRANSITION_MS);
      this.emit("emotion", name);
      this.flashTimer = this.later(() => this.returnToResting(G.FACE_EXPRESSION_TRANSITION_MS),
        G.FACE_EXPRESSION_TRANSITION_MS + holdMs);
    }

    /* --- speaking (face_animator_set_speaking + lip_sync_timer_cb) ----- */
    setSpeaking(on) {
      if (on === this.speaking) return;
      this.speaking = on;
      if (on) {
        this.cancelAntic();
        this.animateTo(POSES.speaking, div(G.FACE_EXPRESSION_TRANSITION_MS, 2));
        this.lipTimer = setInterval(() => {
          const level = this.levelSource ? this.levelSource() : 0;
          let opening = div(level * G.FACE_MOUTH_MAX_OPEN_HEIGHT, 100);
          opening = clamp(opening, 4, G.FACE_MOUTH_MAX_OPEN_HEIGHT);
          this.current.mouth_open_height = opening;
          this.to.mouth_open_height = opening;
        }, G.FACE_LIP_SYNC_REFRESH_MS);
      } else {
        clearInterval(this.lipTimer);
        this.lipTimer = null;
        this.returnToResting(G.FACE_EXPRESSION_TRANSITION_MS);
        this.scheduleIdle();
      }
      this.emit("speaking", on);
    }
  }

  window.DeskBuddyFace = { POSES, W, H, drawFace, blendPose, Animator, theme: T, geometry: G };
})();
