#!/usr/bin/env python3
"""Analyze a NanoWear raw-motion trace (dump_log.py CSV) for gait.

The DEBUG firmware logs {tMillis, total, ax, ay, az, gx, gy, gz}. This script
answers the two questions that matter when the embedded pedometer reads 0:

  1. Is the SENSOR healthy?  (|a| should sit near 1 g if the board is still,
     and track rotation via the gyro.)
  2. Is there actual GAIT in the signal?  (a dominant ~0.5-3 Hz periodicity
     in the accel magnitude — the vertical bounce of walking). If gait is
     present but the reported step total is 0, the embedded pedometer (ST's
     "ML pipeline") is genuinely not counting and the raw trace is your
     ground truth for a software step detector.

Pure Python (no numpy). Usage:
    python3 scripts/analyze_trace.py trace.csv
"""

import sys
import math


def parse(path):
    rows = []
    with open(path) as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            parts = line.split(",")
            if len(parts) != 8:
                continue  # skip [LOG START]/[LOG END]/garbage
            try:
                rows.append([float(p) for p in parts])
            except ValueError:
                continue
    return rows


def main():
    if len(sys.argv) < 2:
        print("usage: analyze_trace.py <trace.csv>")
        sys.exit(1)
    rows = parse(sys.argv[1])
    if len(rows) < 10:
        print(f"not enough data rows ({len(rows)})")
        sys.exit(1)

    ts = [r[0] for r in rows]
    total = [r[1] for r in rows]
    ax = [r[2] for r in rows]
    ay = [r[3] for r in rows]
    az = [r[4] for r in rows]
    gx = [r[5] for r in rows]
    gy = [r[6] for r in rows]
    gz = [r[7] for r in rows]

    dt = (ts[-1] - ts[0]) / max(1, len(ts) - 1)
    fs = 1.0 / dt if dt > 0 else 25.0

    mag = [math.sqrt(ax[i] ** 2 + ay[i] ** 2 + az[i] ** 2) for i in range(len(rows))]
    mean_mag = sum(mag) / len(mag)
    std_mag = math.sqrt(sum((m - mean_mag) ** 2 for m in mag) / len(mag))
    grms = math.sqrt(
        sum(g * g for g in gx + gy + gz) / (3 * len(rows))
    )

    print(f"samples      : {len(rows)}  fs={fs:.2f} Hz  duration={ts[-1]-ts[0]} ms")
    print(f"accel |a|    : mean={mean_mag:.3f} g  std={std_mag:.3f} g  "
          f"(≈1 g when still => sensor healthy, scaling OK)")
    print(f"gyro  RMS    : {grms:.1f} dps  (large => board was moved/shaken)")
    print(f"reported steps: first={total[0]:.0f}  last={total[-1]:.0f}")

    # Detrended magnitude, DFT over the walking/running band.
    d = [m - mean_mag for m in mag]
    N = len(d)
    fmin, fmax, fstep = 0.3, 4.0, 0.02
    best_f, best_a = 0.0, 0.0
    fr = fmin
    while fr <= fmax:
        re = sum(d[n] * math.cos(2 * math.pi * fr * ts[n] / 1000.0) for n in range(N))
        im = sum(d[n] * math.sin(2 * math.pi * fr * ts[n] / 1000.0) for n in range(N))
        a = math.sqrt(re * re + im * im) / N
        if a > best_a:
            best_a, best_f = a, fr
        fr += fstep
    print(f"dominant freq: {best_f:.2f} Hz  (amp={best_a:.3f} g)")
    if 0.5 <= best_f <= 3.0:
        print("  -> inside walking gait band (≈0.5-3 Hz): gait-like periodicity present")
    else:
        print("  -> outside typical walking band: no clear, sustained gait in this capture")

    # Naive step proxy: upward zero-crossings of the detrended vertical axis.
    azm = sum(az) / len(az)
    dz = [v - azm for v in az]
    crossings = sum(1 for i in range(1, len(dz)) if dz[i - 1] < 0 <= dz[i])
    print(f"step proxy   : {crossings} upward zero-crossings of az "
          f"(rough step count from the raw signal)")
    print("  (if this is large but reported steps = 0, the embedded pedometer "
          "is NOT tracking the raw motion)")


if __name__ == "__main__":
    main()
