/*
 * shop.js - "Get the hardware": kits, official store links, affiliate tags.
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 *
 * Store URLs are the official sellers Espressif lists on
 * https://www.espressif.com/en/dev-board/esp32-s3-box-3-en (checked 2026-10-21).
 *
 * AFFILIATE TAGS - placeholder. Fill these in to earn a referral commission;
 * leave them empty and every link goes to the store untagged. The disclosure
 * line under the kits switches on automatically as soon as any tag is set, so
 * a tagged link is never shown without it.
 */
(function () {
  "use strict";

  const AFFILIATE = {
    amazon: "",      /* Amazon Associates tracking ID, e.g. "yourname-20" -> ?tag= */
    aliexpress: "",  /* AliExpress Portals: paste a full generated link to use it instead */
    adafruit: "",    /* Adafruit affiliate: paste a full generated link to use it instead */
  };

  function tagged(store, url) {
    if (store === "amazon" && AFFILIATE.amazon) {
      return url + (url.includes("?") ? "&" : "?") + "tag=" + encodeURIComponent(AFFILIATE.amazon);
    }
    if ((store === "aliexpress" || store === "adafruit") && /^https:\/\//.test(AFFILIATE[store])) {
      return AFFILIATE[store];
    }
    return url;
  }

  const KITS = [
    {
      id: "box3",
      name: "ESP32-S3-BOX-3",
      badge: "Recommended",
      emotion: "happy",
      blurb: "The full kit. Everything this firmware uses, including the sensor dock behind the temperature readout and presence radar.",
      includes: ["BOX-3 main unit: 2.4″ touchscreen, 2 mics, speaker, 16 MB flash + 16 MB PSRAM",
        "BOX-3-SENSOR dock: temperature/humidity, radar, IR, microSD, 18650 slot",
        "BOX-3-DOCK, BOX-3-BRACKET and BOX-3-BREAD adapters"],
      stores: [
        ["espressif", "Espressif", "https://www.espressif.com/en/dev-board/esp32-s3-box-3-en"],
        ["adafruit", "Adafruit", "https://www.adafruit.com/product/5835"],
        ["amazon", "Amazon", "https://www.amazon.com/dp/B0CL6HD8JX"],
        ["digikey", "DigiKey", "https://www.digikey.com/en/products/detail/espressif-systems/ESP32-S3-BOX-3/21556209"],
        ["mouser", "Mouser", "https://www.mouser.com/ProductDetail/Espressif-Systems/ESP32-S3-BOX-3"],
        ["aliexpress", "AliExpress", "https://www.aliexpress.com/item/1005006169541505.html"],
      ],
    },
    {
      id: "box3b",
      name: "ESP32-S3-BOX-3B",
      badge: "Bare bones",
      emotion: "curious",
      blurb: "Main unit and stand only. The face, voice, music and settings all work; the temperature chip hides itself and presence detection stays off.",
      includes: ["BOX-3 main unit", "BOX-3-DOCK stand", "USB-C cable, RGB LED module and test wires"],
      stores: [
        ["adafruit", "Adafruit", "https://www.adafruit.com/product/5883"],
        ["amazon", "Amazon", "https://www.amazon.com/gp/product/B0CSJC5BX6"],
        ["digikey", "DigiKey", "https://www.digikey.com/en/products/detail/espressif-systems/ESP32-S3-BOX-3B/22286690"],
        ["mouser", "Mouser", "https://www.mouser.com/ProductDetail/Espressif-Systems/ESP32-S3-BOX-3B"],
      ],
    },
  ];

  function el(tag, cls, text) {
    const e = document.createElement(tag);
    if (cls) e.className = cls;
    if (text) e.textContent = text;
    return e;
  }

  function render() {
    const host = document.getElementById("kits");
    if (!host) return;
    const F = window.DeskBuddyFace;
    KITS.forEach((kit, i) => {
      const card = el("article", "kit reveal");
      card.style.transitionDelay = i * 120 + "ms";

      const art = el("div", "kit-art");
      const unit = el("div", "mini-device" + (kit.id === "box3" ? " with-dock" : ""));
      const bezel = el("div", "mini-bezel");
      const cv = document.createElement("canvas");
      cv.width = 320; cv.height = 240;
      cv.setAttribute("aria-hidden", "true");
      if (F) {
        const c = cv.getContext("2d");
        c.fillStyle = "#fff"; c.fillRect(0, 0, 320, 240);
        F.drawFace(c, F.POSES[kit.emotion], 0);
      }
      bezel.appendChild(cv);
      unit.appendChild(bezel);
      unit.appendChild(el("div", "mini-base"));
      art.appendChild(unit);
      card.appendChild(art);

      const body = el("div", "kit-body");
      body.appendChild(el("span", "kit-badge", kit.badge));
      body.appendChild(el("h3", null, kit.name));
      body.appendChild(el("p", "kit-blurb", kit.blurb));
      const ul = el("ul", "kit-list");
      kit.includes.forEach((t) => ul.appendChild(el("li", null, t)));
      body.appendChild(ul);
      const stores = el("div", "stores");
      kit.stores.forEach(([key, label, url]) => {
        const a = el("a", "store", label);
        a.href = tagged(key, url);
        a.target = "_blank";
        a.rel = "noopener sponsored";
        a.setAttribute("aria-label", "Buy " + kit.name + " at " + label + " (opens a new tab)");
        stores.appendChild(a);
      });
      body.appendChild(stores);
      card.appendChild(body);
      host.appendChild(card);
    });

    const anyTag = Object.values(AFFILIATE).some(Boolean);
    document.getElementById("disclosure").textContent = anyTag
      ? "Some store links are affiliate links. Buying through them may earn this project a small commission at no extra cost to you."
      : "No affiliate tags are set: these links go straight to the stores and earn this project nothing.";

    /* Cards were added after sim.js wired up the scroll reveals. */
    const io = new IntersectionObserver((es) => es.forEach((e) => {
      if (e.isIntersecting) { e.target.classList.add("in"); io.unobserve(e.target); }
    }), { threshold: 0.15 });
    host.querySelectorAll(".reveal").forEach((n) => io.observe(n));
  }

  window.DeskBuddyShop = { KITS, AFFILIATE };
  if (document.readyState === "loading") document.addEventListener("DOMContentLoaded", render); else render();
})();
