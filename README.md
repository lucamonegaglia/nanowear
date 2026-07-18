# NanoWear — Screenless Ankle-Worn Fitness Tracker

A custom, **screenless, ankle-worn fitness tracker** built around the
**Arduino Nano RP2040 Connect**. It counts steps with a custom **software step
detector** running on the RP2040 — fed by the IMU's raw FIFO accelerometer stream
— and prints them over Serial. The long-term goal is to log
step **and** GPS data on the wearable itself and sync activities to Strava,
using a phone only as a display/interface — never as a pedometer or GPS source.

This repository is an **early-stage prototype**. The board is the only hard
constraint; most subsystem choices are still open (see
[ROADMAP.md](ROADMAP.md)). This file summarizes *what the firmware does today*
and points to the detailed docs for everything else.

---

## What works today

| Feature | Status | Notes |
|---|---|---|
| **Software step counting** | ✅ Working | Custom adaptive peak/threshold detector on the RP2040, fed by the LSM6DSOX FIFO stream (`src/step_detector.{h,cpp}`). The chip's embedded (MLC) pedometer proved unreliable on hardware and is now opt-in behind `NANOWEAR_MLC_PEDOMETER`. |
| **Non-blocking logging loop** | ✅ Working | A `BOOT → LOGGING` state machine polls the step counter every 2 s and prints the cumulative total and per-poll delta to Serial (115200 baud). No `delay()` anywhere. |
| **Resilient step reads** | ✅ Working | I2C transport errors are surfaced (not swallowed); a backward counter reading clamps the delta to zero; a failed read loses/invents no steps. |
| **Host-testable core** | ✅ Working | Step logic sits behind the `IMUSensor` interface with a `MockIMU` double, so the pure logic is unit-tested on the host with no board. |
| **CI** | ✅ Green | Builds the firmware and runs the native test suite on every push/PR. |
| **Switchable comm modes** | ✅ Working | Two build-time modes via the `COM_MODE` macro in `platformio.ini`: BLE RSC streaming to a phone (`nanorp2040connect`) or USB-Serial dump of the in-RAM step ring buffer (`nanorp2040connect-debug`). Mutually exclusive — the BLE radio isn't compiled into the DEBUG build. |

Verified now: `pio test -e native` → **55/55** tests pass; `pio run -e nanorp2040connect`
→ firmware builds (RAM 17.3% / Flash 0.2%). Hardware end-to-end is gated (the
only board is a shared device with no default CI runner).

### What the firmware prints

```
[STATE] BOOT complete -> LOGGING
[STEPDETECT] steps: 42
[STEPDETECT] cadence: 120            # spm, software detector only
[PEDOMETER] Total steps: 42          # accumulator (same total)
[PEDOMETER] New steps this poll: 3   # printed only when delta > 0
```
The `[STEPDETECT]` markers are emitted by the active software detector; the
`[PEDOMETER]` markers are the cumulative accumulator (same total). The
`[STEPDETECT] cadence:` line appears only for the software source.

When not logging (e.g. a sensor-init failure keeps the unit in `BOOT`), it
emits a periodic diagnostic every 5 s instead of silently dropping steps:

```
[STATE] Not logging (1); steps are not being recorded.
```

---

## How the firmware is structured

All hardware access lives behind the `IMUSensor` interface, so the testable
logic never touches I2C:

| Module | Role | Tested on host? |
|---|---|---|
| `src/main.cpp` | `setup`/`loop`; wires the modules, owns the 2 s poll | No (board only) |
| `src/hardware_imu.{h,cpp}` | LSM6DSOX register-level driver + FIFO streaming + opt-in embedded pedometer init | No (board only) |
| `src/step_detector.{h,cpp}` | Software step detector (adaptive peak/threshold on the FIFO stream) | ✅ (native suite) |
| `src/pedometer.{h,cpp}` | Pure step accumulator (total + per-poll delta) | ✅ |
| `src/state_machine.h` | Non-blocking `BOOT → LOGGING → SYNC → LOW_BATTERY` | ✅ |
| `src/elapsed_timer.h` | Non-blocking `millis()` interval timer | ✅ |
| `src/step_codec.h` | Little-endian step-byte assembler | ✅ |
| `src/imu.h` | `IMUSensor` interface + `MockIMU` test double | ✅ |

The `SYNC` and `LOW_BATTERY` states are defined but not yet entered — they are
hook points for future BLE sync and power code. Full layout and the "why
`IMUSensor`, not `IMU`" rationale are in
[CONTRIBUTING.md](CONTRIBUTING.md#repository-layout).

---

## Locked-in hardware & conventions

These are fixed (from [AGENTS.md](AGENTS.md)) and will not change:

- **Board:** Arduino Nano RP2040 Connect (RP2040 + u-blox NINA-W102).
- **Toolchain:** PlatformIO, `platform = raspberrypi`, `board = nanorp2040connect`,
  `framework = arduino`, built with `-O2`.
- **Step counting:** a custom **software step detector** on the RP2040 (the LSM6DSOX
  embedded pedometer proved unreliable on hardware and is now opt-in behind
  `NANOWEAR_MLC_PEDOMETER`).
- **RGB status LED:** driven through the NINA via `WiFiNINA.h` (`LEDR`/`LEDG`/`LEDB`),
  common-anode / **active-low** (`LOW` = ON). *The LED scheme is locked in, but the
  firmware does not drive the LED yet* — it is reserved for future status feedback.
- **Libraries wired in `platformio.ini`:** `WiFiNINA`, `ArduinoBLE`, `TinyGPSPlus`, `Arduino_LSM6DSOX`.

---

## Not implemented yet (roadmap direction, not commitments)

These appear in the product vision but are **not built** — see
[ROADMAP.md](ROADMAP.md) for the full plan and open questions:

- **GPS module** — `TinyGPSPlus` is wired in, but no code reads an external GPS yet.
- **Power management** — battery, charging, deep-sleep with motion wake.
- **Offline storage** — on-board flash buffering of `.gpx`-structured tracks.
- **Strava sync** — phone app uploads the device-built GPX.
- **Haptics** — optional vibration motor.

---

## Build, test, flash

```bash
# Run host unit + simulated end-to-end tests (no board needed)
pio test -e native

# Build the firmware for the Nano RP2040 Connect (BLE RSC streaming, default)
pio run -e nanorp2040connect

# Or the DEBUG build: dump the in-RAM step ring buffer over USB Serial
pio run -e nanorp2040connect-debug

# Flash + run the on-device e2e harness (claims the shared board, builds, flashes)
./scripts/flash-verify.sh                                 # BLE mode
./scripts/flash-verify.sh --env nanorp2040connect-debug   # DEBUG mode
```

PlatformIO lives at `~/.platformio/penv/bin` (or use `python3 -m platformio`).

---

## Where to look next

- **Contributing, branch/commit/PR rules, testing how-to, board-lock usage:**
  [CONTRIBUTING.md](CONTRIBUTING.md)
- **Hardware + software roadmap, decisions, risks, milestones:**
  [ROADMAP.md](ROADMAP.md)
- **GPS module evaluation and step/cadence fusion:** [docs/gps-evaluation.md](docs/gps-evaluation.md)
- **Project identity, hard constraints, engineering principles:**
  [AGENTS.md](AGENTS.md) (same content as `CLAUDE.md`)
