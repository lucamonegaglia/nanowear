# DEBUG ring-buffer dump mode (COM_MODE_DEBUG) — see platformio.ini.
MODE = "debug"


def test_debug_dumps_ring_buffer(ctx):
    # Debug mode records each poll to the in-RAM ring buffer (StepLog) and dumps
    # it over USB Serial on the `l` command. Wait for at least one logged
    # reading, send `l`, and assert the CSV dump is bracketed by [LOG START] /
    # [LOG END] (proving the dump path runs).
    ctx.expect(r"\[MODE\] DEBUG", timeout=15)
    ctx.expect(r"\[PEDOMETER\] Total steps:", timeout=15)
    ctx.write(b"l")  # DebugConsole reads one char per Serial.read(); \n is ignored
    ctx.expect(r"\[LOG START\]", timeout=10)
    ctx.expect(r"\[LOG END\]", timeout=10)
