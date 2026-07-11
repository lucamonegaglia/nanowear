# NanoWear — Screenless Ankle-Worn Fitness Tracker

A custom, **screenless, ankle-worn fitness tracker** built around the
**Arduino Nano RP2040 Connect**. It counts steps with the IMU's onboard
hardware pedometer and prints them over Serial. The long-term goal is to log
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
| **Hardware step counting** | ✅ Working | LSM6DSOX embedded pedometer (MLC), configured via register-level I2C; step-detection routed to INT1. No software step algorithm. |
| **Non-blocking logging loop** | ✅ Working | A `BOOT → LOGGING` state machine polls the step counter every 2 s and prints the cumulative total and per-poll delta to Serial (115200 baud). No `delay()` anywhere. |
| **Resilient step reads** | ✅ Working | I2C transport errors are surfaced (not swallowed); a backward counter reading clamps the delta to zero; a failed read loses/invents no steps. |
| **Host-testable core** | ✅ Working | Step logic sits behind the `IMUSensor` interface with a `MockIMU` double, so the pure logic is unit-tested on the host with no board. |
| **CI** | ✅ Green | Builds the firmware and runs the native test suite on every push/PR. |

Verified now: `pio test -e native` → **23/23** tests pass; `pio run -e nanorp2040connect`
→ firmware builds (RAM 15.7% / Flash 0.2%). Hardware end-to-end is gated (the
only board is a shared device with no default CI runner).

### What the firmware prints

```
[STATE] BOOT complete -> LOGGING
[PEDOMETER] Total steps: 42
[PEDOMETER] New steps this poll: 3
```

When not logging (e.g. a sensor-init failure keeps the unit in `BOOT`), it
emits a periodic diagnostic instead of silently dropping steps.

---

## How the firmware is structured

All hardware access lives behind the `IMUSensor` interface, so the testable
logic never touches I2C:

| Module | Role | Tested on host? |
|---|---|---|
| `src/main.cpp` | `setup`/`loop`; wires the modules, owns the 2 s poll | No (board only) |
| `src/hardware_imu.{h,cpp}` | LSM6DSOX register-level driver + embedded pedometer init | No (board only) |
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
- **Step counting:** the LSM6DSOX **embedded hardware pedometer** — not a software
  algorithm.
- **RGB status LED:** driven through the NINA via `WiFiNINA.h` (`LEDR`/`LEDG`/`LEDB`),
  common-anode / **active-low** (`LOW` = ON). *The LED scheme is locked in, but the
  firmware does not drive the LED yet* — it is reserved for future status feedback.
- **Libraries wired in `platformio.ini`:** `WiFiNINA`, `TinyGPSPlus`, `Arduino_LSM6DSOX`.

---

## Not implemented yet (roadmap direction, not commitments)

These appear in the product vision but are **not built** — see
[ROADMAP.md](ROADMAP.md) for the full plan and open questions:

- **GPS module** — `TinyGPSPlus` is wired in, but no code reads an external GPS yet.
- **BLE RSC peripheral** — advertise steps/cadence to a phone app (RunnerUp/OpenTracks).
- **Power management** — battery, charging, deep-sleep with motion wake.
- **Offline storage** — on-board flash buffering of `.gpx`-structured tracks.
- **Strava sync** — phone app uploads the device-built GPX.
- **Haptics** — optional vibration motor.

---

## Build, test, flash

```bash
# Run host unit + simulated end-to-end tests (no board needed)
pio test -e native

# Build the firmware for the Nano RP2040 Connect
pio run -e nanorp2040connect

# Flash + watch Serial (the board is shared — claim it first)
./scripts/board-lock.sh claim my-agent-id --purpose "verify steps"
pio run -e nanorp2040connect -t upload
pio device monitor -b 115200
./scripts/board-lock.sh release my-agent-id
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
