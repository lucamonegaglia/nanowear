#!/usr/bin/env python3
"""Dump the NanoWear in-RAM step log over USB Serial (debug mode, no BLE).

The firmware keeps a bounded ring buffer of {tMillis, totalSteps} and, in a
paused DEBUG state, can stream it as CSV over the board's USB-Serial port. This
script is the "transfer the .csv to the laptop" half of that: it sends the
debug commands and writes the captured rows to a .csv file on the host.

Typical walk-test flow (board already flashed with the debug firmware):
  1. Reset the counters to a clean baseline, then start logging:
         python3 scripts/dump_log.py --reset
  2. Walk ~50-100 steps with the board on your ankle.
  3. Capture the log to a file:
         python3 scripts/dump_log.py -o steps.csv
     (this enters DEBUG, dumps, and resumes logging automatically)
  4. Inspect:  head steps.csv   # tMillis,totalSteps

Requires pyserial:
    pip install pyserial

The CSV has no header; column 1 = millis() at the poll, column 2 = cumulative
step count. Per-poll deltas are recovered on the laptop (e.g. `pandas` /
`awk`) since the board stores only the cumulative total.
"""

import argparse
import glob
import os
import re
import sys
import time

try:
    import serial
except ImportError:
    sys.exit("error: pyserial is required — run: pip install pyserial")

CSV_RE = re.compile(r"^\d+,\d+$")
SENTINEL_START = "[LOG START]"
SENTINEL_END = "[LOG END]"


def open_port(port, baud):
    if port is None:
        candidates = (
            glob.glob("/dev/ttyACM*")
            + glob.glob("/dev/ttyUSB*")
            + glob.glob("/dev/cu.usbmodem*")
            + glob.glob("/dev/cu.usbserial*")
        )
        if not candidates:
            sys.exit("error: no serial port found; pass --port /dev/ttyXXX")
        port = candidates[0]
        print(f"[dump_log] using auto-detected port {port}")
    try:
        return serial.Serial(port, baud, timeout=2.0)
    except serial.SerialException as e:
        sys.exit(f"error: cannot open {port}: {e}")


def send(ser, cmd):
    ser.write(cmd.encode())


def read_until(ser, sentinel, timeout=5.0):
    """Read lines until `sentinel` (inclusive) is seen; return lines before it."""
    end = time.time() + timeout
    buf = []
    while time.time() < end:
        line = ser.readline().decode(errors="replace").rstrip("\r\n")
        if line == "":
            continue
        if line == sentinel:
            break
        buf.append(line)
    else:
        print(f"[dump_log] warning: timed out waiting for {sentinel}", file=sys.stderr)
    return buf


def dump(ser, out):
    send(ser, "d")  # enter DEBUG (pause polling) so the dump is stable
    time.sleep(0.1)
    send(ser, "l")  # dump the in-RAM log
    lines = read_until(ser, SENTINEL_END)
    if lines and lines[0] == SENTINEL_START:
        lines = lines[1:]
    rows = [ln for ln in lines if CSV_RE.match(ln)]
    text = "\n".join(rows)
    if out:
        with open(out, "w") as f:
            f.write(text + ("\n" if text else ""))
        print(f"[dump_log] wrote {len(rows)} log rows -> {out}")
    else:
        print(text)
    # Resume logging so the board keeps tracking after the transfer, then drain
    # the ack so it doesn't interleave with the next command/session.
    send(ser, "g")
    time.sleep(0.2)
    ser.read_all()
    return rows


def run_action(ser, args):
    if args.reset:
        send(ser, "r")  # zero HW + FW counters + log, resume LOGGING
        time.sleep(0.3)
        print(ser.read_all().decode(errors="replace").rstrip())
    elif args.status:
        send(ser, "s")
        time.sleep(0.3)
        print(ser.read_all().decode(errors="replace").rstrip())
    elif args.resume:
        send(ser, "g")
        time.sleep(0.2)
        print(ser.read_all().decode(errors="replace").rstrip())
    else:
        dump(ser, args.out)


def main():
    ap = argparse.ArgumentParser(description="Extract NanoWear step log over USB Serial.")
    ap.add_argument("-o", "--out", help="write the CSV to this file (default: stdout)")
    ap.add_argument("--reset", action="store_true", help="reset counters + log to 0, resume")
    ap.add_argument("--status", action="store_true", help="print board status and exit")
    ap.add_argument("--resume", action="store_true", help="resume logging (exit DEBUG) and exit")
    ap.add_argument("--port", help="serial port (default: auto-detect)")
    ap.add_argument("--baud", type=int, default=115200, help="baud rate (default 115200)")
    args = ap.parse_args()

    ser = open_port(args.port, args.baud)
    try:
        time.sleep(0.2)  # let the port settle
        run_action(ser, args)
    finally:
        ser.close()


if __name__ == "__main__":
    main()
