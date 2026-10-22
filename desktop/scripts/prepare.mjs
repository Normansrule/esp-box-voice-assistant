/*
 * Assemble desktop/app/ from the website, so the app and the page never drift:
 *
 *   ../web/connect/          -> app/connect/
 *   esptool-js bundle        -> app/connect/vendor/esptool-js.js  (no CDN)
 *   ./firmware/ (optional)   -> app/firmware/  when it holds manifest.json
 *
 * The release workflow drops the release's images into ./firmware before
 * building, so the installer carries the firmware it was released with.
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */

import { cpSync, existsSync, mkdirSync, readFileSync, rmSync, writeFileSync } from "node:fs";
import { createRequire } from "node:module";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";

const here = dirname(dirname(fileURLToPath(import.meta.url)));
const web = join(here, "..", "web", "connect");
const out = join(here, "app");
const SITE = "https://normansrule.github.io/esp-box-voice-assistant/";

rmSync(out, { recursive: true, force: true });
mkdirSync(join(out, "connect", "vendor"), { recursive: true });
cpSync(web, join(out, "connect"), { recursive: true });

const require = createRequire(import.meta.url);
const bundle = join(dirname(require.resolve("esptool-js/package.json")), "bundle.js");
cpSync(bundle, join(out, "connect", "vendor", "esptool-js.js"));

const page = join(out, "connect", "index.html");
let html = readFileSync(page, "utf8");
const cdn = /"esptool-js":\s*"https:\/\/[^"]+"/;
if (!cdn.test(html)) throw new Error("import map for esptool-js not found in web/connect/index.html");
html = html.replace(cdn, '"esptool-js": "./vendor/esptool-js.js"');
html = html.replace('href="../"', `href="${SITE}"`);
writeFileSync(page, html);

const firmware = join(here, "firmware");
if (existsSync(join(firmware, "manifest.json"))) {
  cpSync(firmware, join(out, "firmware"), { recursive: true });
  const manifest = JSON.parse(readFileSync(join(firmware, "manifest.json"), "utf8"));
  console.log(`app/ ready with firmware v${manifest.version}`);
} else {
  console.log("app/ ready without bundled firmware (pick files from a build in the app)");
}
