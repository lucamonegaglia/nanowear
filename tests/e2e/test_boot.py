# On-device e2e: firmware boot
#
# Proves the firmware reaches a running state on the real board — not just that
# it compiles. The device reboots when flashed, so these assertions run against a
# fresh boot.

def test_boot_emits_sentinel(ctx):
    """The stable machine-readable boot sentinel must appear after a reset."""
    ctx.expect(r"\[NW\] BOOT_OK", timeout=15)


def test_boot_reports_definitive_state(ctx):
    """Edge case: the firmware must never silently hang. It must surface a
    definitive state — either it reached LOGGING (BOOT_OK) or it reported a
    sensor failure ([STATE] Not logging) within the window. A device that does
    neither is stuck and this fails."""
    ctx.expect(r"\[NW\] BOOT_OK|\[STATE\] Not logging", timeout=15)
