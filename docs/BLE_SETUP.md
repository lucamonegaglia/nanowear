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
pio test -e native      # 33 tests: RSC codec + MockBlePeripheral (no radio)
```

This covers GATT payload encoding, notify, connection state, and the reset
callback — the entire phone-link logic — without a NINA module or a phone.

## 2. Simulate the device (no board) — phone-side dev

`tools/ble_device_emulator.py` advertises the same GATT surface as the firmware
so you can build/test the *phone side* with no hardware. `bleak` removed its
GATT server in 3.x, so it uses a pinned older `bleak` in an isolated venv:

```bash
python3 -m venv .venv-dev
.venv-dev/bin/pip install -r tools/requirements-device-emulator.txt
sudo .venv-dev/bin/python tools/ble_device_emulator.py
```

Then, in another terminal, run the phone emulator (below) against it.

## 3. Test real connectivity (board + this Linux host)

This machine has a real BLE adapter (`hci0`). Once the board runs the sketch,
`tools/ble_phone_emulator.py` connects over the air and prints live steps/cadence.
It uses the system `bleak` 3.x (already installed on this host):

```bash
sudo python3 tools/ble_phone_emulator.py                 # scan for "NanoWear"
sudo python3 tools/ble_phone_emulator.py --address XX:XX:XX:XX:XX:XX
sudo python3 tools/ble_phone_emulator.py --reset         # send reset command
```

You can also pair with a real phone app (RunnerUp / OpenTracks) — it reads the
RSC service natively. `bluetoothctl` / `gatttool` are also available for
low-level inspection.

### ⚠️ One-time gate: NINA firmware ≥ 3.0.1

The Nano RP2040 Connect's NINA-W102 module needs firmware **≥ 3.0.1** for BLE
(Arduino announced simultaneous Wi-Fi + BLE in Mar 2026; `ArduinoBLE` ≥ 2.0.0
and `WiFiNINA` ≥ 2.0.0 are also required — already set in `platformio.ini`).
The board is connected at `/dev/ttyACM0` but **this environment has no
`esptool` / `arduino-cli`**, so the flash must be done from the Arduino IDE
(or a machine with the tooling). Steps:

1. In the Arduino IDE: **Tools → Board → Arduino Nano RP2040 Connect**.
2. **Tools → WiFi101 / WiFiNINA Firmware Updater** → select the NINA module,
   firmware **≥ 3.0.1**, and **Update Firmware**.
   (CLI equivalent once tooling exists: build `arduino/nina-fw` with
   `RELEASE=1 NANO_RP2040_CONNECT=1`, then flash via
   `SerialNINAPassthrough` + `esptool --before no_reset`.)
3. `pio run -e nanorp2040connect` (already verified to compile) → flash.
4. `sudo python3 tools/ble_phone_emulator.py` to confirm the link.

Until step 1–2 is done, the sketch compiles and the logic is fully tested via
§1/§2, but live over-the-air connection is not yet possible on this board.

## RGB LED caveat

The status LED uses `WiFiNINA` (`LEDB`, active-low): blue = advertising,
off = connected. NINA 3.0.1 moves BLE to SPI and is meant to keep the RGB path
alive, but **verify the LED still works while BLE is active** after the
firmware update (roadmap §6).
