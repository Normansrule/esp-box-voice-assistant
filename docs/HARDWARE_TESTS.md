# Hardware tests: motion sensor and battery

Status of the code these tests check: **UNTESTED ON DEVICE** until a log from this
procedure is committed under `docs/test-logs/`.

You need: the BOX-3 on its **BOX-3-SENSOR** dock with an 18650 cell fitted, the dock
switch **ON**, a USB-C data cable into the **main unit**, and (optional) a multimeter.

## 1. Build and flash

In the **ESP-IDF 5.5 CMD** window:

```bat
cd %USERPROFILE%\Desktop\ESP-BOX-Voice_Assistance
idf.py build
```

Find the port. It is the one called *USB JTAG/serial debug unit*:

```bat
python tools\collect_hw_log.py --list
```

Flash it (use your port):

```bat
idf.py -p COM9 flash
```

## 2. Watch the boot log (optional, 20 seconds)

```bat
idf.py -p COM9 monitor
```

Look for these lines, then press `Ctrl` + `]` to leave:

```
hw_battery: Battery sense on IO10 (ADC1 ch9), divider x4.01
hw_imu: ICM-42607-P (WHO_AM_I 0x60) at 50 Hz
hw_imu: Resting pose: gravity (...)
motion: Motion: ICM-42607-P; battery sense on
BATT,<ms>,<pin_mv>,<battery_mv>,<percent_est>
```

## 3. Record the guided test (about 2 minutes)

Close the monitor first (only one program can hold the port), then:

```bat
python tools\collect_hw_log.py --port COM9
```

It asks for each move in turn: still, tilt left, right, forward, back, shake, pick up,
put down, and a spoken reply (to check the speaker does not trigger "shake"). At the
end it asks for an optional multimeter reading across the cell.

It writes four files to `logs\`:

| File | What |
| --- | --- |
| `hw_<time>.log` | everything the box printed |
| `hw_<time>_imu.csv` | accelerometer, gyro and tilt at 10 Hz, labelled with the step |
| `hw_<time>_batt.csv` | battery readings |
| `hw_<time>_summary.json` | `status`, `motion`, `battery` answers and the gestures seen |

Send the `logs` folder back (it contains no keys or Wi-Fi passwords: the console never
prints them). `logs/` is git-ignored so nothing is published by accident.

## 4. What the logs decide

| Question | Read from | Pass |
| --- | --- | --- |
| Which IMU is fitted | `summary.json` → `motion.who_am_i` | 0x60 (96) ICM-42607-P or 0x67 (103) ICM-42670-P |
| Gravity at rest | `imu.csv` step `rest` | \|a\| ≈ 1.00 g, gyro ≈ 0 |
| Axis directions | steps `tilt_left/right/forward/back` | sign of `tilt_x`/`tilt_y` flips between opposite tilts; tells us whether the gaze needs inverting |
| Shake threshold | step `shake` vs `speaker` | one `Gesture: shake` during `shake`, none during `speaker` |
| Pick up / set down | steps `pick_up`, `set_down` | one of each |
| Battery voltage | `batt.csv` vs multimeter | within about ±50 mV |

Only after these pass do the motion and battery rows in the README move from
**Untested on device** to **Verified on device**.

## Console commands (any serial terminal)

```
status            includes "imu" and battery_mv / battery_pct_est
motion            one reading
motion stream 5   CSV lines at 10 Hz; "motion stop" ends it
motion zero       treat the current pose as level
battery           pin_mv, battery_mv, percent_est, low
```

## Known limits (by design of the hardware)

- **Charging is invisible to the ESP32.** The AP5056 charger's CHRG/STDBY pins only drive
  the dock's red and green LEDs (SENSOR-01 schematic V1.1).
- **Percent is an estimate** from the resting voltage curve in `hw_battery.c`. It reads high
  while charging and dips while the speaker is loud.
- **No cell fitted:** the divider then sees the charger output, so the reading is meaningless.
- Measuring needs the BOX-3-SENSOR dock; on the plain BOX-3-DOCK the battery is reported unavailable.
