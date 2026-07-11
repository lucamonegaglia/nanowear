#!/usr/bin/env python3
# =============================================================================
# run_e2e.py — on-device end-to-end test harness for NanoWear
# =============================================================================
# Flashing is done by scripts/flash-verify.sh. This harness only CONNECTS to the
# board's serial port, prints every line live (the serial terminal monitor), and
# runs one test per firmware feature. Each test asserts that the feature's debug
# output actually appears on the device — proving the feature runs, not just
# compiles.
#
# Per-feature convention: drop a test_<feature>.py in this directory. Define one
# or more functions named test_*(ctx). They are auto-discovered and run in order.
# See README.md for the contract (serial markers the firmware must emit).
#
# Usage:
#   python3 tests/e2e/run_e2e.py --port /dev/ttyACM0 --baud 115200 [--timeout 30]
# =============================================================================
import argparse
import importlib.util
import os
import re
import sys
import threading
import time

import serial

DEFAULT_PORT = "/dev/ttyACM0"
DEFAULT_BAUD = 115200
DEFAULT_TIMEOUT = 30
CONNECT_RETRIES = 30          # x 0.5s — board re-enumerates serial after reset
CONNECT_RETRY_SLEEP = 0.5


class SerialCtx:
    """Shared serial state handed to every test function.

    `lines` holds every line received so far; `start` is this test's cursor into
    it, so tests never steal each other's matches. `expect()` blocks until a
    matching line appears at/after the cursor, or raises AssertionError.
    """

    def __init__(self, lines, lock, start):
        self._lines = lines
        self._lock = lock
        self._start = start

    def expect(self, pattern, timeout=10.0):
        """Return the first line matching `pattern` (regex) from this test's
        cursor onward, advancing the cursor past it. Raise AssertionError on
        timeout so the harness reports a clean FAIL."""
        rx = re.compile(pattern)
        deadline = time.time() + timeout
        while time.time() < deadline:
            with self._lock:
                for i in range(self._start, len(self._lines)):
                    if rx.search(self._lines[i]):
                        self._start = i + 1
                        return self._lines[i]
            time.sleep(0.05)
        raise AssertionError(
            f"timeout ({timeout}s) waiting for /{pattern}/ on serial"
        )


def _reader(ser, lines, lock, stop):
    """Background thread: drain the serial port and mirror it to stdout."""
    while not stop.is_set():
        try:
            raw = ser.readline()
        except serial.SerialException:
            if stop.is_set():
                return
            time.sleep(0.1)
            continue
        if not raw:
            continue
        line = raw.decode(errors="replace").rstrip("\r\n")
        if line == "":
            continue
        with lock:
            lines.append(line)
        print(f"[serial] {line}", flush=True)


def _open_serial(port, baud):
    """Connect, retrying while the board re-enumerates its CDC serial."""
    last_err = None
    for _ in range(CONNECT_RETRIES):
        try:
            return serial.Serial(port, baud, timeout=0.2)
        except serial.SerialException as e:
            last_err = e
            time.sleep(CONNECT_RETRY_SLEEP)
    raise SystemExit(f"could not open serial {port}: {last_err}")


def _discover_tests():
    """Find test_<feature>.py files and collect their test_* functions."""
    here = os.path.dirname(os.path.abspath(__file__))
    collected = []
    for fname in sorted(os.listdir(here)):
        if not (fname.startswith("test_") and fname.endswith(".py")):
            continue
        path = os.path.join(here, fname)
        modname = fname[:-3]
        spec = importlib.util.spec_from_file_location(modname, path)
        mod = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(mod)
        for attr in dir(mod):
            if attr.startswith("test_") and callable(getattr(mod, attr)):
                collected.append((f"{fname}:{attr}", getattr(mod, attr)))
    return collected


def main():
    ap = argparse.ArgumentParser(description="NanoWear on-device e2e harness")
    ap.add_argument("--port", default=DEFAULT_PORT)
    ap.add_argument("--baud", type=int, default=DEFAULT_BAUD)
    ap.add_argument("--timeout", type=float, default=DEFAULT_TIMEOUT,
                    help="default per-test wait for an expected serial line")
    args = ap.parse_args()

    ser = _open_serial(args.port, args.baud)
    # The one-shot boot sentinel prints ~1s after the device resets (on flash).
    # flash-verify.sh launches us immediately after upload, so this settle keeps
    # the reader running across the boot window and we capture it. Keep this
    # short and let the reader thread (started below) own the capture.
    time.sleep(1.0)

    lines = []
    lock = threading.Lock()
    stop = threading.Event()
    reader = threading.Thread(
        target=_reader, args=(ser, lines, lock, stop), daemon=True
    )
    reader.start()

    tests = _discover_tests()
    if not tests:
        stop.set()
        ser.close()
        print("no e2e tests discovered in tests/e2e/")
        sys.exit(1)

    print(f"==> running {len(tests)} on-device e2e test(s) on {args.port}…")

    failures = []
    for name, fn in tests:
        # Each test scans the whole captured session from the start: the device
        # may emit a feature's marker (e.g. the boot sentinel) before the test
        # function begins, so a per-test cursor would miss it.
        ctx = SerialCtx(lines, lock, 0)
        try:
            fn(ctx)
            print(f"PASS  {name}")
        except AssertionError as e:
            print(f"FAIL  {name}: {e}")
            failures.append(name)
        except Exception as e:  # noqa: BLE001 - surface any harness error as FAIL
            print(f"FAIL  {name}: unexpected error: {e}")
            failures.append(name)

    stop.set()
    ser.close()

    if failures:
        print(f"\n{len(failures)}/{len(tests)} e2e test(s) FAILED: {failures}")
        sys.exit(1)
    print(f"\n{len(tests)}/{len(tests)} e2e test(s) PASSED")


if __name__ == "__main__":
    main()
