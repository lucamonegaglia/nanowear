# NanoWear — Screenless Ankle-Worn Fitness Tracker

## Project Identity

A custom, **screenless, ankle-worn fitness tracker** built around the **Arduino Nano RP2040 Connect**. It logs step and GPS data with the eventual goal of syncing to the Strava REST API. Placement above the ankle is intentional — it optimizes for low-noise step counting.

This is an early-stage prototype. The board is the only hard constraint; most subsystem choices are still open (see *Scope vs. Open Questions* below).

## Confirmed / Set in Stone

- **Board:** Arduino Nano RP2040 Connect (RP2040 + u-blox NINA-W102).
- **Toolchain:** PlatformIO (`platform = raspberrypi`, `board = nanorp2040connect`, `framework = arduino`). Build with `-O2`.
- **Step counting:** Onboard ST **LSM6DSOX** 6-axis IMU, using its **embedded hardware pedometer** (Machine Learning Core), not a software step algorithm. See `src/main.cpp` for the register-level init (`FUNC_CFG_ACCESS` bank, `EMB_FUNC_EN_A` PEDO_EN, INT1 routing).
- **Onboard RGB LED:** Driven through the **NINA module via `WiFiNINA.h`** using the constants `LEDR`, `LEDG`, `LEDB`. It is **common-anode / active-low**: `LOW` = ON, `HIGH` = OFF. No other pin definition is valid for this LED.
- **Libraries already wired in `platformio.ini`:** `WiFiNINA`, `TinyGPSPlus`, `Arduino_LSM6DSOX`.
- **Communication style (this assistant):** Neutral information provider. No praise, agreement, or meta-commentary about the user or their input — direct to the substantive answer or code.

## Scope vs. Open Questions (not yet committed)

These appear in the original product vision but are **not fixed**. Treat as direction, not requirements, unless the user confirms:

- **GPS module:** External micro I2C GPS (e.g. PA1010D or SAM-M8Q) — vendor not chosen.
- **Data storage:** Onboard 16MB flash as `.gpx`-structured files — approach unvalidated.
- **Connectivity / sync:** Wi-Fi direct HTTP POST to Strava, or BLE 4.2 smartphone bridge — undecided.
- **Haptics:** Optional GPIO-controlled vibration motor — not yet implemented.
- **Power:** 3.7V 1000mAh LiPo target with ~80–90mA draw — optimize for sleep, but numbers are targets, not specs.

## Code Standards

- Standard, optimized **Arduino C++**. Modular and **highly commented**.
- **Non-blocking architecture only:** no `delay()`; use `millis()` timers or interrupt routines.
- **Memory-efficient:** prioritize low RAM footprint and power-saving/sleep states; efficient sensor polling cycles.
- **Ready to compile** against the PlatformIO config above.
- Register-level IMU work goes through small, named helpers (`writeRegister`/`readRegister` over `Wire`); keep magic numbers explained inline.
