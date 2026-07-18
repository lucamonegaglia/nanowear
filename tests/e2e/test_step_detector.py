# tests/e2e/test_step_detector.py — on-device check for the software step
# detector. Asserts the [STEPDETECT] debug marker appears on the serial port,
# proving the detector runs and is wired into the firmware loop.
def test_step_detector_marker(ctx):
    ctx.expect(r"\[STEPDETECT\] steps:", timeout=15)
