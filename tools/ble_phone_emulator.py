#!/usr/bin/env python3
"""NanoWear "phone" emulator — a BLE central that connects to the tracker.

Acts as the phone: scans for the NanoWear device (by advertised name or the
RSC 0x1814 / custom NanoWear service), connects, reads device capabilities,
subscribes to step + cadence notifications, and prints them live.

Use this to verify REAL connectivity once the board is flashed with NINA
firmware >= 3.0.1 and the NanoWear sketch is running, OR against
tools/ble_device_emulator.py (a host that pretends to be the tracker) so you
can develop the phone side with no board at all.

Requires:  pip install bleak            (uses the system bleak 3.x)
Run as root on Linux (BlueZ D-Bus access):  sudo python3 ble_phone_emulator.py

Examples:
  sudo python3 ble_phone_emulator.py                 # scan for "NanoWear"
  sudo python3 ble_phone_emulator.py --address XX:XX:XX:XX:XX:XX
  sudo python3 ble_phone_emulator.py --name NanoWear --reset

LOCKSTEP WARNING: decode_rsc()/decode_steps() below must stay consistent with
the C++ encoders in src/rsc_codec.h (encodeRscMeasurement / encodeStepCount).
The byte layout here is the *exact* inverse of what the firmware writes, so if
you change a UUID or payload format in the firmware, mirror it here.
"""

import argparse
import asyncio
import struct
import sys

from bleak import BleakScanner, BleakClient

# Full 128-bit UUIDs (SIG 16-bit shortcuts expanded).
RSC_SERVICE = "00001814-0000-1000-8000-00805f9b34fb"
RSC_MEASUREMENT = "00002a53-0000-1000-8000-00805f9b34fb"
RSC_FEATURE = "00002a54-0000-1000-8000-00805f9b34fb"
SENSOR_LOCATION = "00002a5d-0000-1000-8000-00805f9b34fb"
NANO_SERVICE = "9a1b2c3d-0000-4b06-a1b2-3c4d5e6f7a8b"
NANO_STEPS = "9a1b2c3d-0001-4b06-a1b2-3c4d5e6f7a8b"
NANO_CONTROL = "9a1b2c3d-0002-4b06-a1b2-3c4d5e6f7a8b"


def decode_rsc(data: bytearray):
    """RSC Measurement (0x2A53): flags(1) + speed uint16 m/s*256 + cadence(1)."""
    if len(data) < 4:
        return None, None, None
    flags = data[0]
    speed = struct.unpack("<H", data[1:3])[0] / 256.0  # m/s
    cadence_units = data[3]  # 0.5 steps/sec units
    spm = cadence_units * 30  # back to steps/min
    return speed, spm, flags


def decode_steps(data: bytearray) -> int:
    """NanoWear Steps characteristic: uint32 little-endian cumulative steps."""
    return struct.unpack("<I", data[:4])[0]


def on_rsc(_sender: str, data: bytearray):
    speed, spm, flags = decode_rsc(data)
    if spm is None:
        return
    print(f"[RSC]   cadence={spm:5.0f} spm   speed={speed:5.2f} m/s   flags=0x{flags:02x}")


def on_steps(_sender: str, data: bytearray):
    print(f"[STEPS] cumulative={decode_steps(data)}")


async def find_device(name: str, address: str | None):
    if address:
        return address
    print(f"Scanning for '{name}' ...")
    devices = await BleakScanner.discover()
    for d in devices:
        if d.name and name.lower() in d.name.lower():
            print(f"  found {d.name} @ {d.address}")
            return d.address
    # fall back to matching the advertised service UUID
    for d in devices:
        uuids = (d.metadata or {}).get("uuids") or []
        if RSC_SERVICE in uuids or NANO_SERVICE in uuids:
            print(f"  found {d.name} @ {d.address} (by service uuid)")
            return d.address
    print("Device not found. Is it advertising? (board powered + BLE started)")
    return None


async def run(name: str, address: str | None, do_reset: bool, seconds: float):
    addr = await find_device(name, address)
    if not addr:
        return
    async with BleakClient(addr) as client:
        if not client.is_connected:
            print("Failed to connect.")
            return
        print(f"Connected to {addr}. Reading capabilities...")
        try:
            feat = await client.read_gatt_char(RSC_FEATURE)
            loc = await client.read_gatt_char(SENSOR_LOCATION)
            print(f"  RSC Feature = 0x{int.from_bytes(feat, 'little'):04x}  "
                  f"Sensor Location = {loc[0]} (3=Foot)")
        except Exception as e:  # characteristic may be absent on some peers
            print(f"  (capability read skipped: {e})")

        await client.start_notify(RSC_MEASUREMENT, on_rsc)
        await client.start_notify(NANO_STEPS, on_steps)

        if do_reset:
            await client.write_gatt_char(NANO_CONTROL, b"\x01")
            print("[CTRL]  sent reset command (0x01)")

        print(f"Streaming for {seconds:.0f}s (Ctrl-C to stop early) ...")
        try:
            await asyncio.sleep(seconds)
        except asyncio.CancelledError:
            pass
        print("Done.")


def main():
    p = argparse.ArgumentParser(description="NanoWear phone (BLE central) emulator")
    p.add_argument("--name", default="NanoWear", help="advertised device name to find")
    p.add_argument("--address", default=None, help="connect directly to this MAC")
    p.add_argument("--reset", action="store_true", help="send a step-reset command")
    p.add_argument("--seconds", type=float, default=30.0, help="stream duration")
    args = p.parse_args()

    try:
        asyncio.run(run(args.name, args.address, args.reset, args.seconds))
    except KeyboardInterrupt:
        print("\nInterrupted.")
    except Exception as e:
        print(f"Error: {e}", file=sys.stderr)
        sys.exit(1)


if __name__ == "__main__":
    main()
