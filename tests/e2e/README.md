# On-device end-to-end tests (`tests/e2e/`)

These tests verify that firmware **features actually run on the Nano RP2040
Connect**, not just that they compile. They are the project's layer-3 hardware
e2e check (see `CONTRIBUTING.md` → *Testing strategy*).

Cloud CI has no device attached, so these run **locally on the dev PC the board
is wired to**, via:

```bash
./scripts/flash-verify.sh                                     # BLE RSC streaming (default env)
./scripts/flash-verify.sh --env nanorp2040connect-debug       # USB-Serial ring-buffer dump
```

`flash-verify.sh` holds the shared board lock the whole time, then launches
`run_e2e.py`, which opens the serial port, prints every line live (the serial
terminal monitor), and runs one test per feature.

Requires `pyserial` on the dev PC:

```bash
python3 -m pip install -r tests/e2e/requirements.txt
```

## How a run looks

```
==> Flashing firmware to /dev/ttyACM0 …
==> Running on-device e2e harness (tests/e2e)…
[serial] [NW] BOOT_OK
[serial] [PEDOMETER] Total steps: 0
[serial] [NW] BOOT_OK
[serial] [PEDOMETER] Total steps: 0
PASS  test_boot.py:test_boot_emits_sentinel
PASS  test_boot.py:test_boot_reports_definitive_state
PASS  test_pedometer.py:test_pedometer_reports_total

3/3 e2e test(s) PASSED
```

(`[NW] BOOT_OK` is re-emitted on every `LOGGING` poll — it acts as a heartbeat
confirming the device is in `LOGGING`, and guarantees the harness captures it no
matter when the serial monitor attaches.)

## The serial contract

The harness matches **debug lines the firmware prints to `Serial`**. The
firmware must emit stable, machine-readable markers:

| Marker | Meaning | Emitted by |
|--------|---------|------------|
| `[MODE] BLE` | Comm-mode banner: this build streams RSC to a phone over BLE | `src/main.cpp` `setup()` |
| `[MODE] DEBUG` | Comm-mode banner: this build dumps the in-RAM ring buffer | `src/main.cpp` `setup()` |
| `[NW] BOOT_OK` | Firmware is in `LOGGING` (boot succeeded); re-emitted on every `LOGGING` poll as a heartbeat | `src/main.cpp` `loop()` |
| `[STATE] BOOT complete -> LOGGING` | Human-readable equivalent of the above | `src/main.cpp` |
| `[STATE] Not logging (…)` | Boot failed (e.g. IMU not found); device is responsive, not hung | `src/main.cpp` `loop()` |
| `[PEDOMETER] Total steps: <n>` | A polled step total (uint16, 0–65535) | `src/main.cpp` `loop()` |
| `[PEDOMETER] New steps this poll: <n>` | Non-zero delta since last poll | `src/main.cpp` `loop()` |
| `[PEDOMETER] Warning: step-count read failed (I2C error)` | Graceful I2C read failure | `src/main.cpp` `loop()` |
| `[BLE] NINA firmware: <v>` | NINA module firmware version (BLE needs >= 3.0.1) | `src/main.cpp` `setup()` |
| `[BLE] Advertising as 'NanoWear' (RSC 0x1814 + NanoWear steps)` | BLE peripheral up and advertising; re-emitted every poll while no phone is connected (heartbeat), so the harness catches it regardless of attach timing | `src/main.cpp` `setup()` / `loop()` |
| `[BLE] Step reset requested by phone` | Phone sent the reset control write | `src/main.cpp` `handleStepReset()` |
| `[LOG START]` … `[LOG END]` | In-RAM ring-buffer dump (CSV, `<tMillis>,<totalSteps>` per line) | `src/debug_console.cpp` `cmdDump()` |

When you add a firmware feature, add its marker(s) here and assert on them in a
test (below).

## Communication modes (compile-time switch)

The firmware ships two **mutually exclusive** communication modes, chosen at
build time by the `COM_MODE` macro in `platformio.ini`:

| Env | Macro | Behaviour |
|-----|-------|-----------|
| `nanorp2040connect` (default) | `COM_MODE_BLE` | Streams Running Speed & Cadence (RSC `0x1814`) + raw steps to a phone over the NINA BLE radio. Boots advertising as `NanoWear`. |
| `nanorp2040connect-debug` | `COM_MODE_DEBUG` | No BLE. Records each poll to an in-RAM ring buffer (`StepLog`) and dumps it over USB Serial via the `DebugConsole` (`d` pause, `l` dump, `r` reset, `c` clear, `s` status, `?` help). |

Switch modes by flashing the other env — the NINA firmware itself is unchanged
(no `esptool` reflash needed), only the sketch. The BLE radio is not even
compiled into the DEBUG build.

A test module may declare `MODE = "ble"` or `MODE = "debug"` at module level;
`run_e2e.py` reads the firmware's `[MODE]` banner and **skips** tests whose mode
doesn't match the flashed build, so a BLE flash doesn't falsely fail the DEBUG
dump test and vice-versa. Tests without a `MODE` (e.g. `test_boot.py`) run in
both builds.

## Adding a test for a new feature

Create `tests/e2e/test_<feature>.py`. Define one or more functions named
`test_*(ctx)`. They are auto-discovered and run in filename/function order.
`ctx.expect(regex, timeout=…)` blocks until a serial line matching `regex`
appears, then returns the line; it raises `AssertionError` on timeout (a FAIL).

```python
# tests/e2e/test_ble.py
MODE = "ble"
def test_ble_advertises(ctx):
    # Assert the BLE feature's markers show up on-device.
    ctx.expect(r"\[MODE\] BLE", timeout=15)
    ctx.expect(r"\[BLE\] Advertising as 'NanoWear'", timeout=15)
```

Rules:

- **One file per feature** (`test_<feature>.py`), so the suite reads as a list
  of on-device checks, one per capability.
- **Assert on real output**, never on timing alone. A feature "works" iff its
  debug line appears.
- **Cover edge cases**: a failing sensor, a saturated counter (65535), a
  rejected input — assert the firmware *reports* the condition rather than
  hanging. `test_boot.py::test_boot_reports_definitive_state` is the template:
  it fails only if the device says nothing at all (i.e. is stuck).
- Keep `timeout` short (≤15s). The device prints on a fixed cadence; waiting
  longer usually means the marker is missing.

## Re-running without re-flashing

```bash
./scripts/flash-verify.sh --no-flash     # run e2e against current firmware
```

Use this after a manual reset, or to re-run tests without rebuilding. Note the
boot sentinel `[NW] BOOT_OK` only appears after a reset, so for the boot tests
to pass you must reset the board (tap RESET) before `--no-flash`.
