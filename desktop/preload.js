/*
 * The only things the page can ask of the app: hear about serial ports, and
 * say which one to use. See onPortList / choosePort in web/connect/connect.js.
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */

const { contextBridge, ipcRenderer } = require("electron");

contextBridge.exposeInMainWorld("deskBuddyDesktop", {
  onPortList: (callback) => {
    ipcRenderer.removeAllListeners("serial-ports");
    ipcRenderer.on("serial-ports", (_event, ports) => callback(ports));
  },
  choosePort: (portId) => ipcRenderer.send("choose-serial-port", String(portId || "")),
});
