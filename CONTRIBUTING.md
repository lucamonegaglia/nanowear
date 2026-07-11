# NanoWear — Contributing & Development Reference

This is the **detailed** companion to `AGENTS.md` (the project system prompt).
`AGENTS.md` stays lean: identity, hard constraints, code standards, and the
guardrails. Everything procedural — repository layout, branch/commit/PR
conventions with examples, the testing how-to, CI, the board-availability
system, and agentic-development practices — lives here so the always-loaded
system prompt doesn't bloat.

## Quick start

```bash
# Build the firmware for the Nano RP2040 Connect
pio run -e nanorp2040connect

# Run the host unit + simulated end-to-end tests (no board needed)
pio test -e native
```

PlatformIO is installed at `~/.platformio/penv/bin` (use `pio` from there, or
`python3 -m platformio` if it is not on your PATH).

## Repository layout

| Path | Purpose | Compiled on host (`native`)? |
|------|---------|------------------------------|
| `src/main.cpp` | Firmware entry (`setup`/`loop`), wires the modules | No (board only) |
| `src/hardware_imu.{h,cpp}` | LSM6DSOX driver over I2C (register-level) | No (board only) |
| `src/pedometer.{h,cpp}` | Pure step-counting logic (total + delta) | **Yes** |
| `src/elapsed_timer.h` | Non-blocking millisecond timer | **Yes** |
| `src/step_codec.h` | Little-endian step-byte assembler | **Yes** |
| `src/state_machine.h` | Non-blocking `BOOT→LOGGING→SYNC→LOW_BATTERY` | **Yes** |
| `src/imu.h` | `IMUSensor` interface + `MockIMU` test double | **Yes** |
| `test/<suite>/*.cpp` | Native Unity suites (unit + simulated e2e); `<suite>` must start with `test_` | n/a (test code) |
| `scripts/board-lock.sh` | Claim/release the shared board for testing | n/a (tooling) |

The key idea: **all hardware access lives behind the `IMUSensor` interface**,
so the logic in `Pedometer` / `ElapsedTimer` / `StateMachine` /
`combineStepBytes` is testable on the host with a `MockIMU` and no I2C. The
board-only `HardwareIMU` and `main.cpp` are excluded from the native build.

### Why the interface is `IMUSensor`, not `IMU`

The `Arduino_LSM6DSOX` library `#define`s `IMU` to its global object
(`#define IMU IMU_LSM6DSOX`). Naming our interface `IMU` would have that macro
rewrite every `class IMU` / `IMU&` token in the board build into
`IMU_LSM6DSOX`, breaking compilation. The interface is therefore `IMUSensor`
and the test double is `MockIMU`. The concrete `HardwareIMU::begin()` still
calls the library's `IMU.begin()`.

## Branch strategy

- Always branch from `main`.
- Short-lived branches, one logical change each.
- Naming convention:

  | Prefix | Use |
  |--------|-----|
  | `feature/<slug>` | New capability or behaviour |
  | `fix/<slug>` | Bug fix |
  | `chore/<slug>` | Maintenance, deps, tooling |

- Example:
  ```bash
  git switch -c feature/pedometer-unit-tests
  ```

## Commit conventions (Conventional Commits)

Format: `<type>(<scope>): <subject>` (subject imperative, ≤ 72 chars).

| Type | Meaning |
|------|---------|
| `feat` | New feature |
| `fix` | Bug fix |
| `refactor` | Restructure without behaviour change |
| `test` | Add/change tests |
| `docs` | Documentation |
| `ci` | CI / build changes |
| `perf` | Performance |
| `chore` | Misc maintenance |

Examples:
```
feat(pedometer): expose per-poll step delta
test(native): add MockIMU run-loop simulation
ci: build firmware and run native tests on PR
refactor(imu): extract IMUSensor interface from main.cpp
```

## Pull requests

- Open as **draft** while in progress; mark *Ready for review* once CI is green
  and you have self-reviewed.
- Every PR must:
  - Pass CI (firmware builds for the board **and** native tests pass).
  - Add or update tests for any behaviour change.
  - Keep `README.md` in sync: if the PR changes anything user-facing — features,
    supported behaviour, the build/test/flash commands, or the "what works
    today" / "not implemented yet" sections — update `README.md` and tick the
    corresponding box in the PR template. Pure refactors, test-only, or doc-only
    changes that don't alter behaviour generally don't need a README touch-up.
  - Follow the PR template (`.github/pull_request_template.md`).
  - Explain *what* changed, *why*, and *how it was verified*.
  - Be free of merge conflicts — rebase/merge onto the latest `main` and resolve
    any conflict before opening the PR or requesting review.
- Require at least one review. Use `/code-review` to self-review the diff
  before requesting a human review.
- Example end-to-end flow:
  ```bash
  git commit -m "test(native): add MockIMU run-loop simulation"
  git push -u origin feature/pedometer-unit-tests
  gh pr create --draft --fill
  ```

## Testing strategy

Three layers, cheapest first:

1. **Unit tests (host, fast, no hardware)** — `pio test -e native`.
   Pure logic in `Pedometer`, `ElapsedTimer`, `StateMachine`, and
   `combineStepBytes`, driven by `MockIMU`. Runs in CI on every push/PR — the
   primary safety net. Suites live in `test/test_*/` (a `test_`-prefixed
   directory so PlatformIO discovers them explicitly); the native env compiles
   `src/*.cpp` (excluding `main.cpp` / `hardware_imu.cpp`) so the build stays
   hardware-free.
2. **Simulated end-to-end (host)** — a test that replays a *sequence* of
   hardware step readings through the exact `Pedometer.update()` call the
   firmware `loop()` makes, asserting the totals/deltas the firmware would print.
3. **Hardware e2e (board, gated)** — flash + serial check on the physical
   Nano RP2040 Connect. Gated behind a self-hosted runner / manual run (no
   device in default CI).

### Adding a test (example)

Place each suite in a `test_*`-prefixed directory (PlatformIO only auto-discovers
directories whose basename starts with `test_`; the `test/host/` path in older
docs does **not** match that rule and is silently dropped the moment any real
`test_*` suite appears). For example `test/test_pedometer/test_pedometer.cpp`:

```cpp
#include <unity.h>
#include "pedometer.h"
#include "imu.h"

static MockIMU mock;
static Pedometer pedo(mock);

void setUp(void) { mock = MockIMU(); pedo.reset(); }

// If the hardware counter ever reads lower than before (e.g. a missed reset),
// the delta must clamp to zero rather than go negative.
void test_delta_clamped_when_counter_goes_backwards(void) {
    mock.stepCount = 200; pedo.update();
    mock.stepCount = 150;
    TEST_ASSERT_EQUAL_UINT16(0, pedo.update());
    TEST_ASSERT_EQUAL_UINT16(150, pedo.getTotal());
}
```

Rules:
- Put suites under `test/<name>/` where `<name>` starts with `test_`.
- Do **not** define your own `int main()` — PlatformIO auto-generates the Unity
  runner for `test_*` suites. (If it does not in your env, supply one explicitly
  and keep exactly one `main()` per suite.) Adding another `test_*` suite is
  additive; adding a second `*.cpp` to an existing suite just adds tests.
- Test functions named `test_*`; optional `setUp()` / `tearDown()` run around each.
- Assert with `TEST_ASSERT_*`; script sensor behaviour via `MockIMU` (never real
  I2C in host tests).

## Hardware in the loop — board availability & conflict avoidance

The Nano RP2040 Connect is physically connected to this PC over USB, so the
firmware can be flashed and exercised on real hardware here:

```bash
pio run -e nanorp2040connect -t upload   # flash the board
pio device monitor -b 115200             # watch serial output
```

It is the **only** physical board and a *shared* resource: two agents (or two
terminals) flashing/monitoring at once collide — one upload clobbers the other
and serial output interleaves. **Always claim before touching the board and
release after.** Use `scripts/board-lock.sh`:

```bash
# Before flashing / monitoring — fatal if someone else holds it:
./scripts/board-lock.sh claim my-agent-id --purpose "verify step delta"

# Wait for a busy board instead of failing:
./scripts/board-lock.sh claim my-agent-id --purpose "verify step delta" --wait

# Pre-flight in a script: 0 = free/yours, 1 = someone else's:
./scripts/board-lock.sh check my-agent-id

# When finished (same id you claimed with):
./scripts/board-lock.sh release my-agent-id

# Inspect current holder:
./scripts/board-lock.sh status
```

How it works:
- The lock is a file under `.board/` (git-ignored) recording holder id, pid,
  timestamp, and purpose. `claim`/`release` are serialised with `flock`, so
  two agents can never both "win" a free board.
- A lock older than **30 minutes** is treated as *stale* (holder likely
  crashed) and is automatically reclaimable by the next `claim`.
- `<agent_id>` must be unique to you — branch name, Claude Code session id,
  or task id. Pass the same id to `claim` and `release`.
- `claim` does a soft check that a board is visible on a serial port and warns
  (without blocking) if nothing is detected — a flaky detection never stalls
  work, but a missing board means the upload fails, so plug it in first.

Never flash or open a serial monitor unless you hold the lock. To interrupt
someone else, coordinate out-of-band and use `release --force` only as a
human override.

## Agentic development practices

Conventions for working on NanoWear with agents (Claude Code sub-agents,
parallel sessions, or the CLI):

- **One worktree per task.** Use a separate git worktree (Claude Code
  *worktree* mode, or `git worktree add`) per independent line of work.
  Parallel agents then never collide on the working tree and never commit to `main`.
- **Host-test first; board last.** Run `pio test -e native` before any flash.
  Most logic is testable through `IMUSensor` with `MockIMU`, so the board is
  reserved for integration checks, not behaviour verification you could have
  done on the host. This also reduces how often you need the lock.
- **Claim the board before touching it.** `claim` before `upload`/`monitor`,
  `release` immediately after. Don't leave the board locked longer than needed,
  and don't run a long `pio device monitor` in CI.
- **Keep hardware behind the interface.** Add new sensor behaviour behind
  `IMUSensor` (or a similarly injected seam) so it stays unit-testable
  without the device. Prefer pure, time-injected modules (like `ElapsedTimer`)
  over `millis()`-coupled code.
- **Small, reviewable PRs.** One logical change per branch; conventional commits;
  fill `.github/pull_request_template.md`; self-review with `/code-review`
  and attach native-test output (or serial logs) as evidence before human review.
- **Communicate what's on the board.** Set a clear `--purpose` on claim
  (e.g. "verify pedometer delta on ankle") so `status` shows what firmware
  is currently loaded and why.
- **Don't fight the lock.** If `check`/`claim` says the board is taken,
  either `claim … --wait` or move on to host-only work. Never bypass the
  lock by flashing directly.

## Engineering principles (from AGENTS.md)

- **Keep It Stupid Simple (KISS):** the smallest thing that works wins; resist
  cleverness and premature structure.
- **Don't Repeat Yourself (DRY):** extract shared logic once behind a clear
  seam; never copy-paste across modules.
- **Ponytail-style restraint:** before writing code, stop at the first rung
  that holds — *needed at all? already here? in the standard library? a native
  platform feature? an installed dependency? one line?* Only then write the
  minimum. **Delete over add**, fewest files, shortest working diff; no
  abstractions or dependencies nobody asked for. Laziness applies to
  complexity, never to correctness, safety, or hardware calibration.

## CI

`.github/workflows/ci.yml` runs on `push` to `main` and every `pull_request`:
1. Install PlatformIO.
2. `pio run -e nanorp2040connect` — firmware compiles for the board.
3. `pio test -e native` — unit + simulated e2e suites pass.

A PR must be green before review. Hardware e2e is intentionally excluded from
the default runner (no device attached to CI); enable it on a self-hosted
runner when one is available.
