# NanoWear — Hardware & Software Roadmap

> Screenless, ankle-worn fitness tracker on the **Arduino Nano RP2040 Connect**.
> Goal: the **wearable** logs steps **and its own GPS track** on-device (and eventually
> routes to **Strava**). A phone is used **only as an interface** to the wearable — to
> fetch logged data or trigger a sync. The phone is **not** a pedometer, **not** a
> fitness device, and **never** supplies GPS; all sensing lives on the ankle unit.
> Written as a planning document — the board is the only hard constraint; everything
> else below is a *recommendation* to be confirmed when you're back at the keyboard.

---

## 0. TL;DR — the "connect easily with phone" answer

Make the device a **Bluetooth Low Energy (BLE) peripheral** that advertises the
**standard Running Speed and Cadence (RSC) GATT service (UUID `0x1814`)** defined by
the Bluetooth SIG. The wearable carries its **own external GPS module**; the phone is
used **only as a display/interface and the Strava upload bridge** — never as a
pedometer, fitness device, or GPS source.

Why this is the right call:

- The NINA-W102 module on the board **now does BLE** (firmware ≥ 3.0.1, `ArduinoBLE.h`
  ≥ 2.0.0) and can run it **simultaneously with Wi-Fi** over SPI — no custom ESP32
  firmware, no GPIO/jTAG hacks. This was the single biggest blocker and it's gone.
- RSC is a *standard* profile. Any app that supports BLE fitness sensors (including
  open-source ones below) pairs with the device **with zero custom app code** for the
  MVP and shows live steps/cadence as a display.
- **GPS lives on the wearable**, not the phone. An external micro I2C GPS module
  (**PA1010D** / **SAM-M8Q**) is wired to the board, so the device logs position
  independently of any phone — the phone is therefore never a pedometer or GPS source.
- Strava upload is still handled by the **phone app**, so the device never touches
  OAuth/tokens — keeping the ankle unit simple, cheap, and low-power.

**MVP phone app (pick one, both are open source):**
- **RunnerUp** (GPL v3, F-Droid `org.runnerup.free`) — built-in **Strava sync**, BLE +
  ANT+, GPX/TCX export. Best if you want one-tap Strava.
- **OpenTracks** (privacy-first, Codeberg) — robust **GPX 1.1 export**, BLE running
  speed/cadence, no cloud. Best if you want local-first / privacy.

Both read a BLE RSC foot-pod natively (live steps/cadence); for upload they pull the
**device-built GPX** (steps fused with the wearable's own GPS) rather than supplying
any GPS of their own. No custom app required to ship v1.

---

## 1. Fixed constraints (from `AGENTS.md`)

- **Board:** Arduino Nano RP2040 Connect (RP2040 + u-blox NINA-W102). Non-negotiable.
- **Toolchain:** PlatformIO, `platform = raspberrypi`, `board = nanorp2040connect`,
  `framework = arduino`, build `-O2`.
- **Step counting:** a custom **software step detector** on the RP2040, fed by the
  LSM6DSOX FIFO stream (`src/step_detector.{h,cpp}`). The chip's embedded (MLC)
  pedometer proved unreliable on hardware and is now opt-in behind
  `NANOWEAR_MLC_PEDOMETER` (still wired via `writeRegister`/`readRegister`).
- **Onboard RGB LED:** driven through the NINA via `WiFiNINA.h` (`LEDR/LEDG/LEDB`),
  **common-anode / active-low** (`LOW` = ON). Keep this exact scheme.
- **Style:** non-blocking (no `delay()`), `millis()`/interrupts, memory- and
  power-efficient, heavily commented, register-level IMU via small named helpers.

---

## 2. Hardware roadmap

### H0 — Current bench prototype ✅ (mostly done)
- Nano RP2040 Connect on a desk, USB-powered.
- Software step detector running and printing step counts (see `src/main.cpp`).
- **To close out H0:** confirm step counts are sane with the board worn above the
  ankle; verify the FIFO-fed detector stream (and, if the opt-in MLC pedometer is
  enabled, that INT1 actually fires).

### H1 — Power subsystem (first real hardware task)
- **Battery:** 3.7 V Li-Po, ~1000 mAh target (per `AGENTS.md`).
- **Validate the board's battery path first** (research item, see §6): the Nano
  RP2040 Connect has a Li-Po connector (JST), but confirm whether it has **onboard
  charging** or needs an external charger (e.g. **MCP73831**). This decides the BOM.
- **Regulation:** the RP2040/NINA want 3.3 V; size an LDO (e.g. **MCP1700 / AP2112**)
  or buck for the 3.7→3.3 V step, mindful of quiescent current for sleep.
- **Fuel/Battery monitoring:** a divider on a spare ADC pin, or a **MAX17048** fuel gauge.

### H2 — GPS module (external, wired to the board — not on the phone)
Per `AGENTS.md`, GPS is an **external micro I2C module wired to the board** — the phone
never supplies GPS. Two module choices; both are onboard, neither involves the phone:

- **PA1010D** (MiniGPS, ~10 mA) — smaller, lower-power; good default.
- **SAM-M8Q** (better sensitivity, more power) — stronger fix in tough RF / tree cover.
- **(Recommended) u-blox MAX-M10S** — best sensitivity for the bad-sky ankle and
  lowest power in Super-E mode; the strongest standalone choice. Avoid the multi-band
  **NEO-M9N** (power too high, wasted without a good antenna).

`TinyGPSPlus` is already wired in `platformio.ini`. Adds cost, antenna, and power
budget versus a phone-less design, but keeps the wearable fully independent of any
phone for position — which is the point of this product. (The earlier "phone-GPS
bridge" idea is dropped: the phone is only an interface, never a sensor.) See
`docs/gps-evaluation.md` for the full board comparison and how GPS feeds stride
calibration + step/cadence validation.

### H3 — Ankle enclosure & wearability
- 3D-printed or molded **above-ankle strap** enclosure; secure the IMU against the
  limb for low-noise steps (the placement is intentional per `AGENTS.md`).
- **Water/sweat resistance:** IP rating, conformal coat the board, seal the battery.
- Orientation: fix the IMU axis relative to the leg so the pedometer tuning is stable.

### H4 — Haptics & status feedback
- **Vibration motor** (ERM/LRA) on a GPIO with a transistor/FET driver (the IMU/LED
  pins are spoken for — use a free RP2040 GPIO). Lets the phone "ping" the wearer
  (e.g. lap cue). Currently **optional/unimplemented** per `AGENTS.md`.
- Keep the **RGB status LED** as the primary at-a-glance state (paired / logging /
  low-battery) — already wired via NINA.

### H5 — Offline storage (required — the wearable logs on its own)
- Use the onboard **16 MB flash** to buffer `.gpx`-structured files (steps fused with
  the wearable's own GPS) when no phone is in range; flush to the phone on reconnect.
  Approach still unvalidated (§6).
- Because GPS lives on the wearable, the device must store its own track — there is no
  phone to fall back on for position, so H5 is in scope from the start.

### H6 — Integrate / miniaturize
- Move from dev board to a compact custom PCB or a tightly routed protoboard; BOM
  cost, battery life validation against the 80–90 mA target, EMC/antenna check for
  the NINA BLE from the ankle.

---

## 3. Software roadmap

### S0 — Step counting ✅
- Active source is the custom **software step detector** (`src/step_detector.{h,cpp}`),
  fed by the LSM6DSOX FIFO stream through the `SampleConsumer` seam and exposed via
  the `StepSource` seam (`read`/`reset`). The embedded (MLC) pedometer is retained
  but opt-in behind `NANOWEAR_MLC_PEDOMETER` via `IMUSensor::readStepCount()` in
  `src/hardware_imu.{h,cpp}` (see `src/imu.h`). Transport errors are surfaced
  rather than swallowed. Done.
- Close-out: sanity-check counts on the board; tune detector constants if needed.

### S1 — Non-blocking state machine + sleep skeleton
- Replace the flat `loop()` poll with a small **state machine**
  (`BOOT → IDLE → LOGGING → SYNC → LOW_BATTERY`) driven by `millis()` timers and
  interrupts. Already non-blocking; formalize it.
- Single `update()` per subsystem per tick; keep RAM low.

### S2 — BLE RSC peripheral (the core "connect" feature)
- Bump `platformio.ini`: `WiFiNINA` ≥ 2.0.0, add `ArduinoBLE` ≥ 2.0.0, and flash the
  **NINA firmware ≥ 3.0.1** (build `RELEASE=1 NANO_RP2040_CONNECT=1` from
  `arduino/nina-fw`, flash via `SerialNINAPassthrough` + `esptool --before no_reset`).
- Implement a **BLEPeripheral** advertising `0x1814` (RSC) with:
  - **RSC Measurement** (Notify): instantaneous cadence (steps/min) + cumulative steps.
  - **RSC Feature** (Read): advertise what we support.
  - **Sensor Location** (Read): "Foot" / "Ankle".
  - **SC Control Point** (Write/Indicate, optional): reset steps on command.
- Map `getHardwareStepCount()` → RSC Measurement; update on INT1 or every N seconds.
- **Validate the RGB-LED-vs-BLE caveat** (§6): older firmware disabled the SPI RGB
  path under BLE; the 3.0.1 SPI-based BLE is meant to fix this — confirm the LED
  still works while BLE is active.

### S3 — Power management / deep sleep
- Deep-sleep the RP2040; **wake on IMU INT1** (motion) so the unit sips µA at rest.
- Schedule BLE advertising bursts; keep NINA asleep between.
- Tune toward the 80–90 mA active / minimal idle target.

### S4 — Offline buffering + sync (H5)
- Write steps + GPS to flash as rolling `.gpx`/CSV when no phone is in range; on BLE
  reconnect, stream the backlog to the phone app, then clear. Always in scope now that
  the wearable logs its own GPS track.

### S5 — BLE config & control channel
- A small **Device Information / custom config service** over BLE:
  reset steps, set haptics-on-event, read battery %, OTA-friendly handshake.
- Optional **OTA update** of the RP2040 sketch (skip until stable).

### S6 — GPS integration (onboard GPS + step fusion)
- Read `TinyGPSPlus` from the external GPS module, fuse with the step count,
  and build the GPX track **on-device**. The phone is never the GPS source.
- The BLE RSC channel still streams live cadence/steps to the phone as a display; the
  full GPS track is delivered via the offline buffer (S4/H5) or a bulk transfer.

### S7 — Strava path (device logs, phone uploads)
- **No OAuth on the device.** The phone app owns Strava and uploads the **device-built
  GPX** (steps + the wearable's own GPS):
  - RunnerUp: native Strava upload.
  - OpenTracks: export GPX → import to Strava manually or via a sync companion.
- Verify end-to-end: walk → wearable logs steps + GPS → phone fetches GPX → Strava
  activity with correct steps **and** route.

---

## 4. Phone side — open-source options & recommendation

| App | License | Strava sync | BLE RSC | GPS | Notes |
|---|---|---|---|---|---|
| **RunnerUp** | GPL v3 | ✅ built-in | ✅ (BLE+ANT+) | device (wearable) | F-Droid `org.runnerup.free`; GPX/TCX export; no account needed. Best one-tap Strava. |
| **OpenTracks** | Open (privacy) | ❌ (export only) | ✅ running speed/cadence | device (wearable) | Codeberg; GPX 1.1/KML/KMZ export; fully offline; Gadgetbridge integration. Best local-first. |
| **Gadgetbridge** | GPL v3 | via export | ✅ extensible | device (wearable) | Generic device framework; could host a *custom* NanoWear integration for richer control (haptics, config). More work. |
| **OsmAnd + OsmAnd Tracker** | GPL v3 | via export | plugin | device (wearable) | Phone is only a map *viewer* feeding OsmAnd; overkill unless you specifically want OSM mapping. |

**Recommendation:** Ship MVP with **RunnerUp** (if Strava is the priority) or
**OpenTracks** (if privacy/local is). Both are free, open source, and read a BLE RSC
foot-pod with no custom code.

**Alternative — roll your own thin app** (only if you need data the above won't give,
e.g. on-device GPS fusion, custom haptics cues, or a branded UI):
- **Flutter** + `flutter_blue_plus` (Android/iOS from one codebase), or
- **React Native** + `react-native-ble-plx`.
- The app scans for the RSC service, reads live cadence/steps as a display, and pulls
  the **device-built GPX** (steps fused with the wearable's own GPS), then uploads to
  Strava via the **Strava REST API** (OAuth on the phone). The phone never contributes
  GPS. This is more work than using RunnerUp/OpenTracks and is a fallback, not the MVP.

---

## 5. Decisions to confirm on your return (recommendations, not commitments)

1. **Connectivity:** BLE RSC peripheral (recommended) vs Wi-Fi-direct HTTP POST to
   Strava. → **BLE RSC**, because it's standard, low-power, and needs no custom app.
2. **GPS source:** **external module wired to the board** (PA1010D or SAM-M8Q) — now
   committed (see `AGENTS.md`). The phone is **only an interface** and never supplies
   GPS; the earlier "phone-GPS bridge" option is dropped.
3. **Strava:** phone-app sync (recommended) vs on-device OAuth. → **phone-app**; keeps
   the device token-free and simple.
4. **Phone app:** RunnerUp vs OpenTracks vs custom. → **RunnerUp/OpenTracks** for MVP.
5. **Haptics:** include in v1 (H4) or defer. → defer to a later revision unless you
   want lap/event cues early.

---

## 6. Risks & unknowns (research-before-building)

- **Battery path:** confirm whether the Nano RP2040 Connect charges Li-Po onboard or
  needs an external charger IC (MCP73831). Drives H1 BOM.
- **RGB LED under BLE:** NINA 3.0.1 moves BLE to SPI and is *supposed* to free the RGB
  path; still verify the active-low `LEDR/LEDG/LEDB` scheme works while BLE is up.
- **RSC semantics:** some apps treat RSC as a *foot-pod speed* source, not a step
  counter. Verify RunnerUp/OpenTracks surface **cumulative steps + cadence** the way
  we advertise; otherwise add a custom characteristic for raw step count.
- **BLE range/antenna from the ankle:** body wear attenuates 2.4 GHz. Validate
  reliable connection at arm's-length; may need antenna placement care.
- **NINA firmware flash:** DONE — NINA updated to 3.0.1 on 2026-07-11 via `esptool`
  (no Arduino IDE / root needed; exact procedure in `docs/BLE_SETUP.md`). BLE now
  works end-to-end against the live board.
- **Audio to headphones (A2DP):** NOT feasible with the NINA/ArduinoBLE stack —
  A2DP is Bluetooth Classic, but the module exposes only BLE (GATT) + Wi-Fi. Use a
  separate classic-BT audio module if audio cues are wanted. (Evaluated 2026-07-11.)
- **Sleep vs BLE:** deep-sleep + BLE advertising bursts need careful timing so the
  phone doesn't drop the pairing.

---

## 7. Suggested milestones (rough order)

1. **M1 — Confirm & power:** verify step counting on-ankle; resolve H1 battery/charging.
2. **M2 — BLE RSC MVP:** NINA 3.0.1 + `ArduinoBLE`; advertise steps/cadence; pair with
   RunnerUp/OpenTracks; walk → phone shows steps. **(First "connects to phone" win.)**
3. **M3 — Strava E2E:** full walk → BLE → phone app → Strava activity.
4. **M4 — Sleep & wear:** deep-sleep + motion wake; ankle enclosure (H3).
5. **M5 — On-device GPS + storage:** external GPS module (H2), step+GPS fusion on the
   wearable (S6), offline buffering + phone sync (H5/S4).

---

## 8. Verification log

How each change was verified (host tests + board build). Board hardware-e2e is
**N/A** in CI — the Nano RP2040 Connect is a shared device with no default
runner; run it by claiming the board via `scripts/board-lock.sh` and following
the relevant close-out checks (e.g. H0/S0).

### PR #3 — Harden IMU read path (surface transport failures, simplify interface)
- **`pio test -e native`** — PASS (23/23: unit + simulated e2e in
  `test/test_nanowear`; covers pedometer delta clamping, no-loss-on-read-failure,
  and the e2e `loop()` simulation).
- **`pio run -e nanorp2040connect`** — PASS (firmware builds for the board;
  RAM 15.7% / Flash 0.2%).
- **Hardware e2e on the Nano RP2040 Connect** — N/A in CI (no device on default
  runner). To close: claim the board, flash, and confirm sane step counts +
  INT1 step-detection (H0 close-out).

### BLE link — verified end-to-end on hardware (2026-07-11)
- **NINA firmware:** updated to **3.0.1** via `esptool` (board-specific binary
  from `arduino/nina-fw` 3.0.1, flashed through `SerialNINAPassthrough`). No
  Arduino IDE / root required.
- **`pio run -e nanorp2040connect`** — PASS; sketch advertises `NanoWear`
  (RSC `0x1814` + custom steps/control).
- **Hardware e2e:** a BLE central on this host connected, read RSC Feature /
  Sensor Location / Steps, received step + cadence **notifications**, and the
  **reset control write (0x01) was acknowledged** by the board
  (`[BLE] Step reset requested by phone`). Steps read 0 only because the bench
  unit is stationary.
- **Headphones / audio:** evaluated — A2DP is Bluetooth Classic; NINA/ArduinoBLE
  is BLE-only, so streaming audio to headphones is not feasible (plan a separate
  classic-BT module if needed).
- **`pio test -e native`** — PASS (34/34: RSC codec + MockBlePeripheral).

---

## 9. References

- Arduino NINA firmware (`arduino/nina-fw`): https://github.com/arduino/nina-fw
- Wi-Fi + BLE simultaneously on NINA boards (Arduino blog, Mar 2026):
  https://blog.arduino.cc/2026/03/02/you-can-now-use-wi-fi-and-bluetooth-le-simultaneously-on-arduino-nina-based-boards-heres-how/
- BLE Running Speed and Cadence service `0x1814` (Bluetooth SIG):
  https://www.bluetooth.com/specifications/specs/running-speed-and-cadence-service-1-0/
- RunnerUp (GPL v3, F-Droid): https://github.com/jonasoreland/runnerup ·
  https://f-droid.org/packages/org.runnerup.free/
- OpenTracks (Codeberg): https://codeberg.org/OpenTracksApp/OpenTracks
- Gadgetbridge: https://codeberg.org/Freeyourgadget/Gadgetbridge
- Arduino Nano RP2040 Connect tech reference (NINA Wi-Fi + BLE):
  https://docs.arduino.cc/hardware/nano-rp2040-connect
- Strava REST API (uploads): https://developers.strava.com/docs/uploads/
