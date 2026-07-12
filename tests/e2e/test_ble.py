# BLE RSC streaming mode (COM_MODE_BLE) — see platformio.ini.
MODE = "ble"


def test_ble_advertises(ctx):
    # The firmware prints its comm mode at boot, then starts advertising as
    # "NanoWear" (RSC 0x1814 + custom NanoWear steps service). Asserting the
    # advertising line proves the BLE radio came up (needs NINA fw >= 3.0.1).
    ctx.expect(r"\[MODE\] BLE", timeout=15)
    ctx.expect(r"\[BLE\] Advertising as 'NanoWear'", timeout=15)
