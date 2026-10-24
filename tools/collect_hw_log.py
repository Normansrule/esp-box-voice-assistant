#!/usr/bin/env python3
"""
collect_hw_log.py - record real IMU and battery data from a connected BOX-3.

SPDX-License-Identifier: Unlicense OR CC0-1.0

Talks to the firmware's serial console (main/core/buddy_console.c) and walks
you through a short script of moves, saving everything the box says:

    logs/hw_<timestamp>.log         every line from the box, untouched
    logs/hw_<timestamp>_imu.csv     the "IMU,..." stream with the step you were on
    logs/hw_<timestamp>_batt.csv    the "BATT,..." lines
    logs/hw_<timestamp>_summary.json  status / motion / battery answers

Nothing is sent anywhere. Close `idf.py monitor` first: only one program can
hold the port.

Usage (any terminal with Python and pyserial; the ESP-IDF terminal has both):

    python tools/collect_hw_log.py --port COM9
    python tools/collect_hw_log.py --port /dev/ttyACM0
    python tools/collect_hw_log.py --list            # show serial ports
"""

import argparse
import json
import sys
import threading
import time
from datetime import datetime
from pathlib import Path

try:
    import serial
    from serial.tools import list_ports
except ImportError:
    sys.exit("pyserial is missing. Run this from the ESP-IDF terminal, or: pip install pyserial")

ROOT = Path(__file__).resolve().parent.parent
PREFIX = "@buddy "

STEPS = [
    ("rest", 10, "Leave the box sitting still in its dock."),
    ("tilt_left", 6, "Tilt the whole box to the LEFT and hold it there."),
    ("tilt_right", 6, "Tilt it to the RIGHT and hold."),
    ("tilt_forward", 6, "Tilt it FORWARD (screen towards the desk) and hold."),
    ("tilt_back", 6, "Tilt it BACK (screen towards the ceiling) and hold."),
    ("shake", 6, "SHAKE it firmly, side to side, for a couple of seconds."),
    ("pick_up", 6, "PICK IT UP and hold it in your hand, at an angle."),
    ("set_down", 8, "PUT IT BACK in its dock and let go."),
    ("speaker", 8, "Leave it still. (It will say a phrase: checks shake is not set off by its own speaker.)"),
]


class Box:
    def __init__(self, port):
        self.ser = serial.Serial()
        self.ser.port = port
        self.ser.baudrate = 115200
        self.ser.timeout = 0.2
        # Opening with DTR/RTS asserted can reset the chip on some adapters.
        self.ser.dtr = False
        self.ser.rts = False
        self.ser.open()
        self.lines = []
        self.answers = {}
        self.step = "boot"
        self.imu_rows = []
        self.batt_rows = []
        self.running = True
        self.reader = threading.Thread(target=self._read, daemon=True)
        self.reader.start()

    def _read(self):
        buffer = b""
        while self.running:
            chunk = self.ser.read(512)
            if not chunk:
                continue
            buffer += chunk
            while b"\n" in buffer:
                raw, buffer = buffer.split(b"\n", 1)
                line = raw.decode("utf-8", "replace").rstrip("\r")
                stamp = time.time()
                self.lines.append("%.3f %s" % (stamp, line))
                if line.startswith(PREFIX):
                    try:
                        answer = json.loads(line[len(PREFIX):])
                        self.answers.setdefault(answer.get("cmd", "?"), []).append(answer)
                    except ValueError:
                        pass
                elif line.startswith("IMU,"):
                    self.imu_rows.append([self.step] + line.split(",")[1:])
                elif line.startswith("BATT,"):
                    self.batt_rows.append([self.step] + line.split(",")[1:])

    def send(self, command, wait=1.0):
        self.ser.write((command + "\n").encode())
        self.ser.flush()
        time.sleep(wait)

    def close(self):
        self.running = False
        self.reader.join(timeout=1)
        self.ser.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--port", help="serial port of the box, e.g. COM9 or /dev/ttyACM0")
    parser.add_argument("--list", action="store_true", help="list serial ports and exit")
    parser.add_argument("--quick", action="store_true", help="skip the guided moves; just read status")
    args = parser.parse_args()

    if args.list or not args.port:
        for p in list_ports.comports():
            print("%-14s %s" % (p.device, p.description))
        if not args.port:
            print("\nPass the box's port with --port (it is usually 'USB JTAG/serial debug unit').")
        return 0

    out_dir = ROOT / "logs"
    out_dir.mkdir(exist_ok=True)
    stamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    base = out_dir / ("hw_" + stamp)

    box = Box(args.port)
    print("Connected to %s. Talking to the box..." % args.port)
    try:
        box.send("status")
        box.send("motion")
        box.send("battery")
        if not box.answers:
            print("No answer from the console. Is the new firmware flashed, and is idf.py monitor closed?")

        st = (box.answers.get("status") or [{}])[-1]
        print("Firmware %s, IMU: %s, dock: %s, battery: %s mV (~%s%% estimated)"
              % (st.get("version"), st.get("imu"), st.get("dock"),
                 st.get("battery_mv", "n/a"), st.get("battery_pct_est", "n/a")))

        if not args.quick:
            box.send("motion stream 5", 0.5)   # 10 lines a second
            for name, seconds, text in STEPS:
                box.step = name
                print("\n[%s] %s" % (name, text))
                if name == "speaker":
                    box.send("say tell me a joke", 0)
                for left in range(seconds, 0, -1):
                    print("   %2d s " % left, end="\r", flush=True)
                    time.sleep(1)
            box.step = "end"
            box.send("motion stop")
            box.send("battery")

        meter = input("\nOptional: multimeter reading across the 18650 cell in volts (Enter to skip): ").strip()
    finally:
        box.close()

    base.with_suffix(".log").write_text("\n".join(box.lines) + "\n", encoding="utf-8")
    header = "step,ms,ax_g,ay_g,az_g,gx_dps,gy_dps,gz_dps,tilt_x_deg,tilt_y_deg,temp_c\n"
    Path(str(base) + "_imu.csv").write_text(header + "".join(",".join(r) + "\n" for r in box.imu_rows),
                                            encoding="utf-8")
    Path(str(base) + "_batt.csv").write_text("step,ms,pin_mv,battery_mv,percent_est\n" +
                                             "".join(",".join(r) + "\n" for r in box.batt_rows), encoding="utf-8")
    summary = {"port": args.port, "answers": box.answers, "imu_rows": len(box.imu_rows),
               "batt_rows": len(box.batt_rows), "multimeter_v": meter or None,
               "gestures_logged": [l for l in box.lines if "Gesture:" in l]}
    Path(str(base) + "_summary.json").write_text(json.dumps(summary, indent=1), encoding="utf-8")

    print("\nSaved %s.log, _imu.csv (%d rows), _batt.csv, _summary.json in %s"
          % (base.name, len(box.imu_rows), out_dir))
    print("Gestures the box reported: %d" % len(summary["gestures_logged"]))
    print("Share the logs folder (no secrets in it: the console never prints keys or Wi-Fi passwords).")
    return 0


if __name__ == "__main__":
    sys.exit(main())
