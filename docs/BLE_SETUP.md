# NanoWear BLE — phone link setup & testing

This documents the Bluetooth Low Energy "connect to phone" feature
(`src/ble_peripheral.h`, `src/rsc_codec.h`, `src/ble_peripheral_arduino.cpp`)
and how to test it **without** and **with** the physical board.

## What the device advertises

The tracker is a **BLE peripheral** so open-source phone apps
(RunnerUp, OpenTracks) pair with zero custom app code. It exposes:

| Service | UUID | Characteristic | UUID | Prop | Meaning |
|---|---|---|---|---|---|
| RSC (SIG) | `0x1814` | RSC Measurement | `0x2A53` | Notify | flags(0) + speed(0) + cadence |
| RSC (SIG) | `0x1814` | RSC Feature | `0x2A54` | Read | `0x0000` (cadence only) |
| RSC (SIG) | `0x1814` | Sensor Location | `0x2A5D` | Read | `0x03` = Foot (ankle) |
| NanoWear (custom) | `9a1b2c3d-0000-...` | Steps | `...-0001-...` | Read+Notify | uint32 LE cumulative steps |
| NanoWear (custom) | `9a1b2c3d-0000-...` | Control | `...-0002-...` | Write | `0x01` = reset steps |

The custom **Steps** characteristic is the authoritative raw-step channel.
Some apps surface RSC cadence but not cumulative steps (see `ROADMAP.md` §6),
so we expose both. The firmware derives cadence from the rate of step updates.

> The custom vendor UUIDs are **placeholders** (`src/ble_peripheral_arduino.cpp`).
> Replace them with a UUID registered to you before shipping.

## 1. Simulate / unit-test (no board, no radio) — always available

All link logic sits behind the `BlePeripheral` interface (mirroring
`IMUSensor`/`MockIMU`), so it is host-testable:

```bash
pio test -e native      # 34 tests: RSC codec + MockBlePeripheral (no radio)
```

This covers GATT payload encoding, notify, connection state, and the reset
callback — the entire phone-link logic — without a NINA module or a phone.

## 2. Simulate the device (no board) — currently blocked here

`tools/ble_device_emulator.py` advertises the same GATT surface as the firmware
so you can build/test the *phone side* with no hardware. It needs a BLE **GATT
server**, which requires root (BlueZ D-Bus) and a `bleak` version that still
ships `BleakServer` (removed after ~0.21). **This environment can't run it** (no
sudo password; the pinned `bleak==0.22.3` lacks `BleakServer`). For a host-side
loop, run the phone emulator (§3) against the **real board** instead — BLE is
live, so that is the faster path.

## 3. Test real connectivity (board + this Linux host)

This machine has a real BLE adapter (`hci0`). Once the board runs the sketch (see
§"NINA firmware" below), connect over the air and stream live steps/cadence.
`bleak` is **not** preinstalled system-wide, but installs cleanly into a venv and
runs **without root** (the adapter is accessible to the `lucam` user):

```bash
python3 -m venv .venv-phone
.venv-phone/bin/pip install bleak
.venv-phone/bin/python tools/ble_phone_emulator.py                 # scan for "NanoWear"
.venv-phone/bin/python tools/ble_phone_emulator.py --address 58:BF:25:3B:A5:9A
.venv-phone/bin/python tools/ble_phone_emulator.py --reset         # send reset command
```

You can also pair with a real phone app (RunnerUp / OpenTracks) — it reads the
RSC service natively. `bluetoothctl` / `gatttool` are also available for
low-level inspection.

### NINA firmware ≥ 3.0.1 — required, and updatable here

The NINA-W102 module needs firmware **≥ 3.0.1** for BLE (`ArduinoBLE` ≥ 2.0.0
and `WiFiNINA` ≥ 2.0.0 are already set in `platformio.ini`). **This environment
can perform the update without the Arduino IDE** — `esptool` installs via pip and
the board-specific binary is on GitHub. The board's NINA was updated to
**3.0.1 on 2026-07-11**, so BLE is live; the steps below are for re-doing it
(e.g. after a NINA wipe or on a fresh board).

1. Install esptool and download the Nano RP2040 Connect binary:
   ```bash
   pip install esptool
   curl -L -o nina.bin \
     https://github.com/arduino/nina-fw/releases/download/3.0.1/NINA-arduino.mbed_nano.nanorp2040connect.bin
   ```
2. Flash the `SerialNINAPassthrough` example (from the WiFiNINA library) so the
   RP2040 bridges USB ↔ NINA, then write the firmware over the same port:
   ```bash
   # build + upload the passthrough bridge (any PlatformIO terminal)
   pio ci <WiFiNINA>/examples/Tools/SerialNINAPassthrough \
       --lib="WiFiNINA" -e nanorp2040connect --target upload

   # flash the NINA (SerialNINAPassthrough must still be running)
   esptool --port /dev/ttyACM0 --baud 115200 --before no_reset \
     write_flash --flash-mode dio --flash-freq 40m --flash-size 4MB 0x0 nina.bin
   ```
   Use `--before no_reset` (not `default_reset`) for this board. esptool v5 renamed
   the flags (`--flash-mode` etc., `no-reset`); the old names still work
   (warnings only). `/dev/ttyACM0` is world-writable, so **no root/sudo needed**.
3. Re-flash the NanoWear sketch: `pio run -e nanorp2040connect -t upload`. It
   advertises `NanoWear` immediately.

### Verified end-to-end (2026-07-11)

After the NINA update, the full link was confirmed against the **live board**
from this Linux host (real BLE adapter `hci0`, no phone):

- Discovered + connected: `NanoWear @ 58:BF:25:3B:A5:9A`.
- GATT surface correct: RSC `0x1814` + custom steps (Read/Notify) + control (Write).
- Reads: `RSC_Feature=0x0000` (cadence-only), `SensorLocation=3` (Foot/ankle), `Steps=0`.
- **Notifications received**: cumulative steps + cadence streamed to the central.
- **Reset control (0x01) acknowledged** by the board
  (`[BLE] Step reset requested by phone`).

Re-check any time with the §3 `ble_phone_emulator.py` commands.

## RGB LED caveat

The status LED uses `WiFiNINA` (`LEDB`, active-low): blue = advertising,
off = connected. NINA 3.0.1 moves BLE to SPI and is meant to keep the RGB path
alive; the LED was observed working while BLE was active after the 2026-07-11
firmware update (the active-low `LEDR/LEDG/LEDB` scheme is intact).

### Audio to headphones — not feasible with this stack

A2DP (streaming audio to headphones) is a **Bluetooth Classic** profile. The
NINA-W102 firmware + `ArduinoBLE` expose only **BLE (GATT) + Wi-Fi**, so the
board cannot send audio to headphones without a full custom ESP-IDF firmware
rewrite of the NINA module (a separate project). A BLE headphone may appear in a
scan (e.g. `LE_WH-XB900N` was seen during testing), but that is the control
channel, not audio. For audio cues, plan a **separate classic-BT audio module**
fed from the RP2040.
