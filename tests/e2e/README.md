# On-device end-to-end tests (`tests/e2e/`)

These tests verify that firmware **features actually run on the Nano RP2040
Connect**, not just that they compile. They are the project's layer-3 hardware
e2e check (see `CONTRIBUTING.md` → *Testing strategy*).

Cloud CI has no device attached, so these run **locally on the dev PC the board
is wired to**, via:

```bash
./scripts/flash-verify.sh            # claim board -> build -> flash -> run e2e
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
| `[NW] BOOT_OK` | Firmware is in `LOGGING` (boot succeeded); re-emitted on every `LOGGING` poll as a heartbeat | `src/main.cpp` `loop()` |
| `[STATE] BOOT complete -> LOGGING` | Human-readable equivalent of the above | `src/main.cpp` |
| `[STATE] Not logging (…)` | Boot failed (e.g. IMU not found); device is responsive, not hung | `src/main.cpp` `loop()` |
| `[PEDOMETER] Total steps: <n>` | A polled step total (uint16, 0–65535) | `src/main.cpp` `loop()` |
| `[PEDOMETER] New steps this poll: <n>` | Non-zero delta since last poll | `src/main.cpp` `loop()` |
| `[PEDOMETER] Warning: step-count read failed (I2C error)` | Graceful I2C read failure | `src/main.cpp` `loop()` |

When you add a firmware feature, add its marker(s) here and assert on them in a
test (below).

## Adding a test for a new feature

Create `tests/e2e/test_<feature>.py`. Define one or more functions named
`test_*(ctx)`. They are auto-discovered and run in filename/function order.
`ctx.expect(regex, timeout=…)` blocks until a serial line matching `regex`
appears, then returns the line; it raises `AssertionError` on timeout (a FAIL).

```python
# tests/e2e/test_ble.py
def test_ble_advertises(ctx):
    # Assert the new BLE feature's marker shows up on-device.
    ctx.expect(r"\[BLE\] advertising", timeout=15)
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
