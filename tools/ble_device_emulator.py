#!/usr/bin/env python3
"""NanoWear device emulator — a BLE peripheral that pretends to be the tracker.

This host advertises the SAME GATT surface the real NanoWear firmware exposes
(RSC 0x1814 + custom NanoWear service) so you can develop and test the *phone
side* (tools/ble_phone_emulator.py, or any RSC app like RunnerUp/OpenTracks)
with zero board hardware.

NOTE: bleak removed its GATT *server* from 3.x, so this script requires a
pinned older bleak. Set it up in an isolated venv so it never clobbers the
system bleak 3.x that the phone emulator uses:

    python3 -m venv .venv-dev
    .venv-dev/bin/pip install -r tools/requirements-device-emulator.txt
    sudo .venv-dev/bin/python tools/ble_device_emulator.py

Requires root on Linux (BlueZ D-Bus). Ctrl-C to stop.

The characteristics mirror src/rsc_codec.h + src/ble_peripheral_arduino.cpp:
  RSC Service 0x1814
  ...

LOCKSTEP WARNING: encode_rsc()/encode_steps() below must stay byte-for-byte
consistent with the C++ encoders in src/rsc_codec.h (encodeRscMeasurement /
encodeStepCount) and the cadence derivation deriveCadenceSpm(). If you change a
GATT UUID or the payload layout in the firmware, change it here too — a phone
app getting a different byte order will silently misread steps/cadence.
    - RSC Measurement 0x2A53  Notify : flags + speed(0) + cadence
    - RSC Feature     0x2A54  Read   : 0x0000 (cadence only)
    - Sensor Location  0x2A5D  Read   : 0x03 (Foot / ankle)
  NanoWear custom service (placeholder vendor UUID)
    - Steps  Notify+Read : uint32 LE cumulative step count
    - Control Write      : 0x01 = reset steps
"""

import asyncio
import random
import struct

from bleak import BleakServer

RSC_SERVICE = "00001814-0000-1000-8000-00805f9b34fb"
RSC_MEASUREMENT = "00002a53-0000-1000-8000-00805f9b34fb"
RSC_FEATURE = "00002a54-0000-1000-8000-00805f9b34fb"
SENSOR_LOCATION = "00002a5d-0000-1000-8000-00805f9b34fb"
NANO_SERVICE = "9a1b2c3d-0000-4b06-a1b2-3c4d5e6f7a8b"
NANO_STEPS = "9a1b2c3d-0001-4b06-a1b2-3c4d5e6f7a8b"
NANO_CONTROL = "9a1b2c3d-0002-4b06-a1b2-3c4d5e6f7a8b"

# Mutable state shared with the write handler.
STATE = {"steps": 0, "last": 0, "last_ts": 0.0}


def encode_rsc(steps_now: int, now: float):
    """Match rsc_codec.h: flags=0, speed=0, cadence = spm/30."""
    cadence_spm = 0
    if STATE["last_ts"] and now > STATE["last_ts"]:
        delta = max(0, steps_now - STATE["last"])
        dt = now - STATE["last_ts"]
        if dt > 0:
            cadence_spm = int(delta * 60 / dt)
    STATE["last"] = steps_now
    STATE["last_ts"] = now
    units = cadence_spm // 30
    return bytes([0x00, 0x00, 0x00, units])


def encode_steps(steps: int) -> bytes:
    return struct.pack("<I", steps)


# Write handler (0x01 on Control => reset steps). Signature matches bleak's
# BLECharacteristic write callback: (characteristic, value_bytes).
def on_control_write(_characteristic, data):
    if data and data[0] == 0x01:
        STATE["steps"] = 0
        print("[emulator] reset command received -> steps zeroed")


async def main():
    server = BleakServer()
    server.name = "NanoWear"

    # --- RSC service ---
    rsc = await server.add_new_service(RSC_SERVICE)
    await rsc.add_characteristic(
        RSC_FEATURE, properties=["read"], value=bytes([0x00, 0x00]),
        permissions=["read"],
    )
    await rsc.add_characteristic(
        RSC_MEASUREMENT, properties=["read", "notify"],
        value=encode_rsc(0, 0.0), permissions=["read"],
    )
    await rsc.add_characteristic(
        SENSOR_LOCATION, properties=["read"], value=bytes([0x03]),
        permissions=["read"],
    )

    # --- custom NanoWear service ---
    nano = await server.add_new_service(NANO_SERVICE)
    await nano.add_characteristic(
        NANO_STEPS, properties=["read", "notify"], value=encode_steps(0),
        permissions=["read"],
    )
    await nano.add_characteristic(
        NANO_CONTROL, properties=["write"], value=None,
        permissions=["write"], handler=on_control_write,
    )

    await server.start()
    print("Device emulator advertising as 'NanoWear'. Ctrl-C to stop.")

    try:
        while True:
            await asyncio.sleep(2)
            # Simulate walking: 0..40 new steps per 2s.
            STATE["steps"] += random.randint(0, 40)
            now = asyncio.get_running_loop().time()
            await server.update_value(RSC_SERVICE, RSC_MEASUREMENT, encode_rsc(STATE["steps"], now))
            await server.update_value(NANO_SERVICE, NANO_STEPS, encode_steps(STATE["steps"]))
            print(f"[emulator] steps={STATE['steps']}")
    except KeyboardInterrupt:
        pass
    finally:
        await server.stop()


if __name__ == "__main__":
    asyncio.run(main())
