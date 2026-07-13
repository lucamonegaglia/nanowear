# DEBUG ring-buffer dump mode (COM_MODE_DEBUG) — see platformio.ini.
MODE = "debug"


def test_debug_dumps_ring_buffer(ctx):
    # Debug mode records a raw motion trace (tMillis,total,ax,ay,az,gx,gy,gz) to
    # the in-RAM ring buffer (StepLog) and dumps it over USB Serial on the `l`
    # command. Wait for at least one logged reading, send `l`, and assert the
    # dump is bracketed by [LOG START] / [LOG END] and contains an 8-column
    # motion-trace row.
    ctx.expect(r"\[MODE\] DEBUG", timeout=15)
    ctx.expect(r"\[PEDOMETER\] Total steps:", timeout=15)
    ctx.write(b"l")  # DebugConsole reads one char per Serial.read(); \n is ignored
    ctx.expect(r"\[LOG START\]", timeout=10)
    ctx.expect(r"^\d+,\d+(?:,-?\d+(?:\.\d+)?){6}$", timeout=10)  # a motion-trace row
    ctx.expect(r"\[LOG END\]", timeout=10)
