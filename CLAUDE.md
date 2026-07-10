# NanoWear — Engineering Standards & Agentic Practices

This file defines how we build, test, and ship NanoWear. It is the single
source of truth for branch/PR/commit conventions and the testing strategy. The
project identity and hard constraints live in `AGENTS.md`; the product/feature
plan lives in `ROADMAP.md`.

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
| `src/state_machine.h` | Non-blocking `BOOT→IDLE→LOGGING→SYNC→LOW_BATTERY` | **Yes** |
| `src/imu.h` | `IMU` interface + `MockIMU` test double | **Yes** |
| `test/host/*.cpp` | Native Unity suites (unit + simulated e2e) | n/a (test code) |

The key idea: **all hardware access lives behind the `IMU` interface**, so the
logic in `Pedometer` / `ElapsedTimer` / `StateMachine` / `combineStepBytes` is
testable on the host with a `MockIMU` and no I2C. The board-only `HardwareIMU`
and `main.cpp` are excluded from the native build.

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
refactor(imu): extract IMU interface from main.cpp
```

## Pull requests

- Open as **draft** while in progress; mark *Ready for review* once CI is green
  and you have self-reviewed.
- Every PR must:
  - Pass CI (firmware builds for the board **and** native tests pass).
  - Add or update tests for any behaviour change.
  - Follow the PR template (`.github/pull_request_template.md`).
  - Explain *what* changed, *why*, and *how it was verified*.
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
   `combineStepBytes`, driven by `MockIMU`. Runs in CI on every push/PR. This
   is the primary safety net.

2. **Simulated end-to-end (host)** — a test that replays a *sequence* of
   hardware step readings through the exact `Pedometer.update()` call the
   firmware `loop()` makes, asserting the totals and deltas the firmware would
   print. Catches integration regressions in the polling logic without a board.

3. **Hardware e2e (board, gated)** — flashing the firmware and checking serial
   output on the physical Nano RP2040 Connect. Gated behind a self-hosted
   runner or manual run because it requires the device; it is **not** part of
   the default CI.

### Adding a test (example)

Create `test/host/test_pedometer.cpp`:

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
- File name `test_*.cpp`; test functions named `test_*`.
- Optional `setUp()` / `tearDown()` run around each test.
- Assert with `TEST_ASSERT_*` from Unity.
- Use `MockIMU` to script sensor behaviour — never real I2C in host tests.

## Code standards (from AGENTS.md, reinforced)

- **Non-blocking only:** no `delay()`; drive polling with `ElapsedTimer` /
  `StateMachine` or interrupts so the MCU stays free for sleep / other work.
- **Memory-efficient:** `-O2`, low RAM, efficient sensor cycles.
- **Highly commented & modular:** register-level IMU work through small named
  helpers; keep magic numbers explained inline.
- **Hardware behind an interface:** keep logic testable by depending on `IMU`,
  not on `Wire` directly.

## CI

`.github/workflows/ci.yml` runs on `push` to `main` and on every `pull_request`:

1. Install PlatformIO.
2. `pio run -e nanorp2040connect` — the firmware compiles for the board.
3. `pio test -e native` — unit + simulated e2e suites pass.

A PR must be green before review. Hardware e2e is intentionally excluded from
the default runner (no device attached to CI); enable it on a self-hosted
runner when one is available.
