// Captures the README screenshots from the simulator site (never a device).
// Usage, from the repository root:
//   python tools/build_site.py
//   python -m http.server 8765 --directory _site &
//   node tools/capture_readme_screens.js            (needs: npm i playwright)
// Writes PNGs to docs/assets/screens/. Convert/compress as you like.
const path = require("path");
const { chromium } = require("playwright");

const BASE = process.env.SITE_URL || "http://localhost:8765/";
const OUT = path.join(__dirname, "..", "docs", "assets", "screens");
const HIDE = "#nav{display:none!important} .reveal{opacity:1!important;transform:none!important}";

(async () => {
  const browser = await chromium.launch();
  const page = await browser.newPage({ viewport: { width: 1280, height: 900 }, deviceScaleFactor: 2 });
  const shot = async (sel, name) => {
    const el = await page.$(sel);
    await el.scrollIntoViewIfNeeded();
    await page.waitForTimeout(1200);
    // Page coordinates (boundingBox() is relative to the viewport).
    const b = await el.evaluate((n) => { const r = n.getBoundingClientRect();
      return { x: r.left + scrollX, y: r.top + scrollY, width: r.width, height: r.height }; });
    const m = 40; // breathing room around the section
    await page.screenshot({ path: path.join(OUT, name), fullPage: true,
      clip: { x: Math.max(0, b.x - m), y: Math.max(0, b.y - m), width: b.width + 2 * m, height: b.height + 2 * m } });
    console.log("wrote", name);
  };

  await page.goto(BASE);
  await page.addStyleTag({ content: HIDE });
  await page.waitForTimeout(2000);
  await shot("#listen .wrap", "voice-flow.png");
  await shot("#inside .wrap", "inside.png");

  // Simulator mid-conversation: wake, then ask for a joke.
  await page.locator("#simulator").scrollIntoViewIfNeeded();
  await page.getByRole("button", { name: /Say .Hi E\.S\.P\./ }).click();
  await page.waitForTimeout(900);
  await page.getByRole("button", { name: "tell me a joke", exact: true }).first().click();
  await page.waitForTimeout(1600);
  await shot("#simulator .wrap", "simulator.png");

  await page.goto(BASE + "connect/");
  await page.addStyleTag({ content: HIDE });
  await page.waitForTimeout(1500);
  await page.screenshot({ path: path.join(OUT, "connect.png") });
  console.log("wrote connect.png");

  await browser.close();
})();
