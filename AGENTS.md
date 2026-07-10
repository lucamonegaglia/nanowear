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
- **Motion-sensor access behind an interface:** all firmware logic depends on the `IStepSensor` interface (`src/imu.h`), not on `Wire` directly. The board-only `HardwareIMU` is the only place that talks I2C. The `Arduino_LSM6DSOX` library exposes a global `IMU` object, which is why our interface is deliberately *not* named `IMU` (see `CONTRIBUTING.md` → *Repository layout*).
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

## Repository Infrastructure & Architecture Guardrails

### Git & Branching Strategy

- NEVER commit directly to the `main` branch under any circumstances.
- For every task, optimization, or feature implementation, you must explicitly create a separate feature branch or isolated git worktree (e.g., `feature/mlc-pedometer-tuning`).
- Once a subsystem is verified, stage the files, create a structured commit on your feature branch, push the branch to the remote origin, and notify the user to initiate a Pull Request.

### Build Pipelines

- Always use `pio run` to verify compilation sanity before packaging a commit.
- Target upload architecture remains constrained to standard Mbed OS targets.

## Engineering standards & workflows

Branch / commit / PR / testing / CI conventions are summarized here and
documented in full in **[CONTRIBUTING.md](CONTRIBUTING.md)**:

- **Branch:** branch from `main`; one logical change per short-lived branch
  (`feature/<slug>`, `fix/<slug>`, `chore/<slug>`). Use a worktree per task.
- **Commits:** Conventional Commits, `<type>(<scope>): <subject>`
  (`feat`/`fix`/`refactor`/`test`/`docs`/`ci`/`perf`/`chore`).
- **PRs:** open as draft; require green CI, tests for behaviour changes, and
  the PR template (`.github/pull_request_template.md`); self-review with
  `/code-review` before human review.
- **Testing (3 layers):** (1) host unit tests `pio test -e native` via
  `MockStepSensor`; (2) simulated end-to-end; (3) gated hardware e2e on
  the board. See *Testing strategy* in CONTRIBUTING.md.
- **CI:** `.github/workflows/ci.yml` builds the firmware and runs native
  tests on every push/PR.

## Hardware in the loop

The Nano RP2040 Connect is **physically connected to this PC over USB**, so
firmware can be flashed and verified on real hardware here. It is the
**only** board and a *shared* resource — claim it before flashing or
monitoring and release after, via `scripts/board-lock.sh` (see
*Hardware in the loop* in CONTRIBUTING.md). Host-test first; board last.

## Agentic development

Conventions for working here with agents (Claude Code sub-agents, parallel
sessions, or the CLI): one worktree per task, host-test before any flash,
claim the board before touching it, keep hardware behind the `IStepSensor`
interface, and small reviewable PRs. Full list in *Agentic development
practices* (CONTRIBUTING.md).
