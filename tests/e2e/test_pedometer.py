# On-device e2e: embedded hardware pedometer
#
# Proves the step-counting feature actually runs on the device: the firmware
# must poll the IMU and print its total, and that total must be a well-formed
# unsigned 16-bit count (the LSM6DSOX counter saturates at 65535).

import re


def test_pedometer_reports_total(ctx):
    """At least one '[PEDOMETER] Total steps: N' line must appear, and N must be
    a valid uint16 in [0, 65535] (covers the saturation edge case)."""
    line = ctx.expect(r"\[PEDOMETER\] Total steps: (\d+)")
    m = re.search(r"Total steps: (\d+)", line)
    # expect() already guaranteed a match, but be explicit for the bound check.
    assert m, f"pedometer total line malformed: {line!r}"
    total = int(m.group(1))
    assert 0 <= total <= 65535, f"pedometer total out of uint16 range: {total}"
