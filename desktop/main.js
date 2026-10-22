/*
 * Desk Buddy Connect - desktop app.
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 *
 * A window around web/connect/, which already does all the work through the
 * Web Serial API. Electron supports that API but has no built-in port chooser,
 * so this file supplies the two things a browser normally does:
 *
 *   - permission to use serial ports, granted only to the bundled page
 *   - the list of ports, sent to the page, which draws its own chooser
 *
 * The page, esptool-js and the firmware are all inside the app (see
 * scripts/prepare.mjs), so it works with no internet connection.
 */

const { app, BrowserWindow, ipcMain, net, protocol, shell } = require("electron");
const path = require("node:path");
const { pathToFileURL } = require("node:url");

const ROOT = path.join(__dirname, "app");
const ORIGIN = "app://buddy";
const SITE = "https://normansrule.github.io/esp-box-voice-assistant/";
const ESPRESSIF_VID = "303a";

protocol.registerSchemesAsPrivileged([
  { scheme: "app", privileges: { standard: true, secure: true, supportFetchAPI: true } },
]);

/** Serve files from app/, and nothing outside it. */
function serveBundledFiles() {
  protocol.handle("app", (request) => {
    const relative = decodeURIComponent(new URL(request.url).pathname);
    const file = path.normalize(path.join(ROOT, relative));
    if (!file.startsWith(ROOT + path.sep)) {
      return new Response("Not found", { status: 404 });
    }
    return net.fetch(pathToFileURL(file).toString());
  });
}

function describePort(port) {
  return {
    portId: port.portId,
    portName: port.portName,
    displayName: port.displayName || port.portName,
    vendorId: port.vendorId,
    productId: port.productId,
  };
}

function createWindow() {
  const win = new BrowserWindow({
    width: 1200,
    height: 880,
    minWidth: 380,
    minHeight: 500,
    title: "Desk Buddy Connect",
    backgroundColor: "#FFFFFF",
    autoHideMenuBar: true,
    webPreferences: {
      preload: path.join(__dirname, "preload.js"),
      contextIsolation: true,
      sandbox: true,
      nodeIntegration: false,
    },
  });

  const session = win.webContents.session;
  const fromApp = (origin) => typeof origin === "string" && origin.startsWith(ORIGIN);

  session.setPermissionCheckHandler((_contents, permission, origin) =>
    permission === "serial" && fromApp(origin));
  session.setDevicePermissionHandler((details) =>
    details.deviceType === "serial" && fromApp(details.origin));

  /*
   * navigator.serial.requestPort() lands here. The callback must be called
   * exactly once, with a port id or "" for "cancelled".
   */
  let pendingChoice = null;
  let ports = [];
  const sendPorts = () => {
    const sorted = [...ports].sort((a, b) =>
      (b.vendorId === ESPRESSIF_VID) - (a.vendorId === ESPRESSIF_VID));
    win.webContents.send("serial-ports", sorted.map(describePort));
  };

  session.on("select-serial-port", (event, portList, _contents, callback) => {
    event.preventDefault();
    if (pendingChoice) pendingChoice("");
    pendingChoice = callback;
    ports = portList;
    sendPorts();
  });
  session.on("serial-port-added", (_event, port) => {
    ports = [...ports.filter((p) => p.portId !== port.portId), port];
    if (pendingChoice) sendPorts();
  });
  session.on("serial-port-removed", (_event, port) => {
    ports = ports.filter((p) => p.portId !== port.portId);
    if (pendingChoice) sendPorts();
  });
  ipcMain.on("choose-serial-port", (event, portId) => {
    if (event.sender !== win.webContents || !pendingChoice) return;
    const known = ports.some((p) => p.portId === portId);
    pendingChoice(known ? portId : "");
    pendingChoice = null;
  });

  // Links leave the app for the real browser; the window only ever shows the page.
  win.webContents.setWindowOpenHandler(({ url }) => {
    if (url.startsWith("https://")) shell.openExternal(url);
    return { action: "deny" };
  });
  win.webContents.on("will-navigate", (event, url) => {
    if (url.startsWith(ORIGIN + "/connect/")) return;
    event.preventDefault();
    shell.openExternal(url.startsWith("https://") ? url : SITE);
  });

  win.loadURL(ORIGIN + "/connect/index.html");
}

app.whenReady().then(() => {
  serveBundledFiles();
  createWindow();
});

app.on("window-all-closed", () => app.quit());
