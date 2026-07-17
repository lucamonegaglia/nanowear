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
- **Motion-sensor access behind an interface:** all firmware logic depends on the `IMUSensor` interface (`src/imu.h`), not on `Wire` directly. The board-only `HardwareIMU` is the only place that talks I2C. The `Arduino_LSM6DSOX` library exposes a global `IMU` object, which is why our interface is deliberately *not* named `IMU` (see `CONTRIBUTING.md` → *Repository layout*).
- **Communication style (this assistant):** Neutral information provider. No praise, agreement, or meta-commentary about the user or their input — direct to the substantive answer or code.

## Scope vs. Open Questions (not yet committed)

These appear in the original product vision but are **not fixed**. Treat as direction, not requirements, unless the user confirms:

- **GPS module:** External micro I2C GPS **module wired to the board** (e.g. PA1010D or SAM-M8Q) — vendor not chosen. The GPS lives **on the wearable itself**, never on the phone.
- **Data storage:** Onboard 16MB flash as `.gpx`-structured files — approach unvalidated.
- **Connectivity / sync:** Wi-Fi direct HTTP POST to Strava, or BLE 4.2 bridge to a smartphone — undecided. The phone is used **only as an interface/display for the wearable** (e.g. to fetch logged data or trigger a sync); it is **not** a pedometer or fitness device, and it never supplies GPS.
- **Haptics:** Optional GPIO-controlled vibration motor — not yet implemented.
- **Power:** 3.7V 1000mAh LiPo target with ~80–90mA draw — optimize for sleep, but numbers are targets, not specs.

## Code Standards

- Standard, optimized **Arduino C++**. Modular and **highly commented**.
- **Non-blocking architecture only:** no `delay()`; use `millis()` timers or interrupt routines.
- **Memory-efficient:** prioritize low RAM footprint and power-saving/sleep states; efficient sensor polling cycles.
- **Ready to compile** against the PlatformIO config above.
- Register-level IMU work goes through small, named helpers (`writeRegister`/`readRegister` over `Wire`); keep magic numbers explained inline.

## Engineering Principles

- **Keep It Stupid Simple (KISS):** the smallest thing that works wins; resist cleverness and premature structure.
- **Don't Repeat Yourself (DRY):** extract shared logic once behind a clear seam; never copy-paste across modules.
- **Ponytail-style restraint:** before writing code, stop at the first rung that holds — *needed at all? already here? in the standard library? a native platform feature? an installed dependency? one line?* Only then write the minimum. **Delete over add**, fewest files, shortest working diff; no abstractions or dependencies nobody asked for. Laziness applies to complexity, never to correctness, safety, or hardware calibration.

## Repository Infrastructure & Architecture Guardrails

### Git & Branching Strategy

- NEVER commit directly to the `main` branch under any circumstances.
- For every task, optimization, or feature implementation, you must explicitly create a separate feature branch or isolated git worktree (e.g., `feature/mlc-pedometer-tuning`).
- Once a subsystem is verified, stage the files, create a structured commit on your feature branch, push the branch to the remote origin, and notify the user to initiate a Pull Request.

### Build Pipelines

- Always use `pio run` to verify compilation sanity before packaging a commit.
- Build target is fixed to the RP2040 / Raspberry Pi Pico-SDK toolchain (`platform = raspberrypi`); do not switch frameworks.

## Engineering standards & workflows

Branch / commit / PR / testing / CI conventions are summarized here and
documented in full in **[CONTRIBUTING.md](CONTRIBUTING.md)**:

- **Branch:** branch from `main`; one logical change per short-lived branch
  (`feature/<slug>`, `fix/<slug>`, `chore/<slug>`). Use a worktree per task.
- **Commits:** Conventional Commits, `<type>(<scope>): <subject>`
  (`feat`/`fix`/`refactor`/`test`/`docs`/`ci`/`perf`/`chore`).
- **PRs:** open as draft; require green CI, tests for behaviour changes, the
  PR template (`.github/pull_request_template.md`), and a `README.md` update
  when the PR changes user-facing features or behaviour; self-review with
  `/code-review` before human review.
- **Testing (3 layers):** (1) host unit tests `pio test -e native` via
  `MockIMU`; (2) simulated end-to-end; (3) **on-device e2e** — flash the board
  and run one test per feature that asserts its serial debug output appears, via
  `./scripts/flash-verify.sh` + `tests/e2e/`. See *Testing strategy* in
  CONTRIBUTING.md. **Each new feature must ship its own `tests/e2e/test_<feature>.py`.**
- **CI:** `.github/workflows/ci.yml` builds the firmware and runs native
  tests on every push/PR. CI cannot flash (no device in the cloud); hardware
  e2e is run locally via `scripts/flash-verify.sh` on the dev PC the board is
  wired to.

## Hardware in the loop

The Nano RP2040 Connect is **physically connected to this PC over USB**, so
firmware can be flashed and verified on real hardware here. It is the
**only** board and a *shared* resource — claim it before flashing or
monitoring and release after, via `scripts/board-lock.sh` (see
*Hardware in the loop* in CONTRIBUTING.md). Host-test first; board last.

To flash the board and run the per-feature on-device tests in one shot:

```bash
./scripts/flash-verify.sh          # claim -> build -> flash -> run tests/e2e
```

It prints the board's serial output live (the serial terminal monitor) and
fails the run if any feature's debug output does not appear. Re-run tests
without re-flashing with `./scripts/flash-verify.sh --no-flash`.

## Agentic development

Conventions for working here with agents (Claude Code sub-agents, parallel
sessions, or the CLI): one worktree per task, host-test before any flash,
claim the board before touching it, keep hardware behind the `IMUSensor`
interface, and small reviewable PRs. Full list in *Agentic development
practices* (CONTRIBUTING.md).

### Per-branch context summary (`context_summary.md`)

The context window is small and agents run sequentially on the same task, so
each branch / worktree keeps a **`context_summary.md`** at its root as a running
memory shared by every agent that works on that task. Rules:

- **Write only what is verified and load-bearing for this task/feature.** No
  speculation, no unverified guesses, no raw log dumps. Capture findings a
  future agent can act on without re-deriving them.
- **Evict or correct stale info.** If something previously written turns out to
  be wrong, inaccurate, misleading, or no longer relevant, edit it out or fix
  it — the file is a living summary, not an append-only log.
- **Read it first, update it last.** At the start of a session read
  `context_summary.md` (if present) before exploring; before finishing, write
  back any new verified facts so the next agent starts warm.
- **Never commits to the main repo.** This file is local to the branch/worktree
  and must not land in a PR. Add `context_summary.md` (and `**/context_summary.md`)
  to `.gitignore` so it is never staged or pushed.

## Agent operating rules (hard requirements)

### Open PRs only through `/open-pr` — never `gh pr create` directly
A hook blocks `gh pr create` unless a fresh review of the **exact current diff**
is on record. Always open PRs via the `/open-pr` command, which enforces the
intended "self-review with /code-review" flow (see *PRs* above) and does it with
a **fresh-context reviewer**:

1. `/open-pr` spawns a **fresh subagent** (new context — it has not seen the
   coding conversation, so it reviews without author bias) that runs
   `/code-review` on the branch diff vs `main` and returns a must-fix / nice-to-have list.
2. Address every **must-fix** finding (edit, build, verify with `pio test -e native`).
3. `/open-pr` records the review and opens a **draft** PR.

Skipping this (running `gh pr create` yourself) will be denied by the hook.
The point of a separate reviewer is that the author is blind to their own mistakes.

### Choose plan mode autonomously
Decide for yourself whether to enter plan mode — do not ask unless genuinely
blocked:
- **Enter plan mode** (`EnterPlanMode`) for non-trivial work: new features,
  multi-file or architecturally significant changes, ambiguous approaches, or
  anything the user should sign off on before code is written.
- **Skip plan mode** for trivial, well-specified changes: typos, one-line bug
  fixes, obvious config tweaks.
- When in doubt, prefer presenting a plan first.
