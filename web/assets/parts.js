/*
 * parts.js - "Inside the box": the BOX-3 hardware, each fact with its source.
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 *
 * Sources, all from Espressif:
 *   BSP     managed_components/espressif__esp-box-3/include/bsp/esp-box-3.h
 *   SENSOR  hardware/SCH_ESP32-S3-BOX-3_V1.0/SCH_ESP32-S3-BOX-3-SENSOR-01_V1.1 (esp-box repo)
 *   PRODUCT https://www.espressif.com/en/dev-board/esp32-s3-box-3-en
 */
(function () {
  "use strict";

  const PARTS = [
    { icon: "chip", name: "ESP32-S3", where: "Main unit",
      fact: "Dual-core 240 MHz with vector instructions for speech models. 16 MB flash and 16 MB octal PSRAM on the WROOM-1 module.",
      use: "Runs everything: wake word, phrases, face, Wi-Fi.", used: true, src: "PRODUCT" },
    { icon: "screen", name: "2.4″ touchscreen", where: "Main unit",
      fact: "320×240 LCD (ILI9341-class driver in the BSP) with a capacitive touch controller.",
      use: "The face, captions, settings. Tap to poke.", used: true, src: "BSP" },
    { icon: "mic", name: "Two microphones", where: "Main unit",
      fact: "Digital mics into an ES7210 audio ADC; the speaker runs from an ES8311 codec.",
      use: "Wake word, commands, lip sync from the speaker level.", used: true, src: "BSP" },
    { icon: "gyro", name: "Motion sensor", where: "Main unit, I2C 0x68",
      fact: "6-axis ICM-42607-P (accelerometer + gyroscope) on the shared I2C bus, SCL GPIO18 / SDA GPIO8.",
      use: "New: eyes follow a tilt, shake makes it dizzy, picking it up surprises it. Untested on device.", used: true, src: "BSP" },
    { icon: "switch", name: "Mute switch", where: "Top edge",
      fact: "A hardware switch on GPIO1, read as a button.",
      use: "Stops microphone capture; the face keeps going.", used: true, src: "BSP" },
    { icon: "thermo", name: "Temperature & humidity", where: "BOX-3-SENSOR dock",
      fact: "AHT30 on its own I2C pair, IO40 (SCL) / IO41 (SDA).",
      use: "The readout chip in the top-right corner.", used: true, src: "SENSOR" },
    { icon: "radar", name: "Presence radar", where: "BOX-3-SENSOR dock",
      fact: "AT581X microwave radar on the same dock I2C bus.",
      use: "Looks up, quietly, when someone sits down.", used: true, src: "PRODUCT" },
    { icon: "battery", name: "18650 battery slot", where: "BOX-3-SENSOR dock",
      fact: "AP5056 charger (about 500 mA), DW01-A + 8205A protection, ON/OFF switch. VBAT reaches GPIO10 through a 301k/100k divider (÷4.01).",
      use: "New: voltage and a ~percent estimate on screen, low-battery warning. Charging is LED-only. Untested on device.", used: true, src: "SENSOR" },
    { icon: "ir", name: "IR transmitter & receiver", where: "BOX-3-SENSOR dock",
      fact: "For learning and sending remote-control codes.",
      use: "Not used by this firmware yet.", used: false, src: "PRODUCT" },
    { icon: "sd", name: "microSD slot", where: "BOX-3-SENSOR dock",
      fact: "SPI mode, card detect wired.",
      use: "Not used yet: music lives in flash.", used: false, src: "SENSOR" },
  ];

  const SRC = {
    BSP: ["Espressif BOX-3 BSP header", "https://github.com/espressif/esp-bsp/tree/master/bsp/esp-box-3"],
    SENSOR: ["BOX-3-SENSOR schematic V1.1", "https://github.com/espressif/esp-box/tree/master/hardware/SCH_ESP32-S3-BOX-3_V1.0"],
    PRODUCT: ["Espressif product page", "https://www.espressif.com/en/dev-board/esp32-s3-box-3-en"],
  };

  /* Tiny line icons, drawn in the brand red. */
  const ICONS = {
    chip: '<rect x="7" y="7" width="10" height="10" rx="2"/><path d="M9 3v4M15 3v4M9 17v4M15 17v4M3 9h4M3 15h4M17 9h4M17 15h4"/>',
    screen: '<rect x="3" y="5" width="18" height="12" rx="2"/><path d="M9 21h6M12 17v4"/>',
    mic: '<rect x="9" y="3" width="6" height="11" rx="3"/><path d="M5 11a7 7 0 0 0 14 0M12 18v3"/>',
    gyro: '<circle cx="12" cy="12" r="8"/><ellipse cx="12" cy="12" rx="8" ry="3"/><circle cx="12" cy="12" r="1.6"/>',
    switch: '<rect x="3" y="8" width="18" height="8" rx="4"/><circle cx="16" cy="12" r="2.5"/>',
    thermo: '<path d="M10 14V5a2 2 0 0 1 4 0v9a4 4 0 1 1-4 0z"/>',
    radar: '<path d="M4 12a8 8 0 0 1 16 0M7 12a5 5 0 0 1 10 0M10 12a2 2 0 0 1 4 0"/><path d="M12 12v8"/>',
    battery: '<rect x="3" y="8" width="16" height="9" rx="2"/><path d="M21 11v3M6 11v3M9 11v3"/>',
    ir: '<circle cx="7" cy="12" r="2.5"/><path d="M12 8a6 6 0 0 1 0 8M15 6a9 9 0 0 1 0 12"/>',
    sd: '<path d="M7 3h7l4 4v14H7z"/><path d="M10 3v4M13 3v4"/>',
  };

  function render() {
    const host = document.getElementById("parts");
    if (!host) return;
    PARTS.forEach((p, i) => {
      const d = document.createElement("details");
      d.className = "part reveal" + (p.used ? "" : " unused");
      d.style.transitionDelay = (i % 5) * 60 + "ms";
      const s = document.createElement("summary");
      s.innerHTML = '<svg viewBox="0 0 24 24" aria-hidden="true" fill="none" stroke="currentColor" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round">'
        + ICONS[p.icon] + "</svg><span><b></b><small></small></span>";
      s.querySelector("b").textContent = p.name;
      s.querySelector("small").textContent = p.where;
      d.appendChild(s);
      const body = document.createElement("div");
      body.className = "part-body";
      const f = document.createElement("p"); f.textContent = p.fact;
      const u = document.createElement("p"); u.className = "part-use"; u.textContent = p.use;
      const a = document.createElement("a"); a.href = SRC[p.src][1]; a.textContent = "Source: " + SRC[p.src][0];
      a.target = "_blank"; a.rel = "noopener";
      body.append(f, u, a);
      d.appendChild(body);
      host.appendChild(d);
    });
    const io = new IntersectionObserver((es) => es.forEach((e) => {
      if (e.isIntersecting) { e.target.classList.add("in"); io.unobserve(e.target); }
    }), { threshold: 0.1 });
    host.querySelectorAll(".reveal").forEach((n) => io.observe(n));
  }

  if (document.readyState === "loading") document.addEventListener("DOMContentLoaded", render); else render();
})();
