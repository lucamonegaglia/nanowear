#ifndef NANOWEAR_BLE_PERIPHERAL_ARDUINO_H
#define NANOWEAR_BLE_PERIPHERAL_ARDUINO_H

#include "ble_peripheral.h"

// ---------------------------------------------------------------------------
// ArduinoBlePeripheral — NINA-W102 BLE driver for the Nano RP2040 Connect
// ---------------------------------------------------------------------------
// Concrete BlePeripheral built on the ArduinoBLE library (>= 2.0.0), which
// talks to the u-blox NINA-W102 module over SPI. Requires NINA firmware
// >= 3.0.1 (see docs/BLE_SETUP.md) for BLE support.
//
// GATT surface:
//   * RSC Service           0x1814 (SIG standard)
//       - RSC Measurement   0x2A53  Notify : flags + speed(0) + cadence
//       - RSC Feature       0x2A54  Read   : capability bitmask (cadence only)
//       - Sensor Location    0x2A5D  Read   : 0x03 "Foot" (ankle)
//   * NanoWear custom service (vendor UUID, placeholder — register before ship)
//       - Steps             Notify+Read : uint32 LE cumulative step count
//       - Control           Write      : 0x01 = request step reset
//
// The custom Steps characteristic is the authoritative raw-step channel; some
// phone apps surface RSC cadence but not cumulative steps (see ROADMAP.md §6),
// so we expose both. The driver derives cadence from the rate of step updates
// it sees via notifySteps(), keeping the interface simple (total steps only).
//
// NOTE: this header pulls in <ArduinoBLE.h>; it is included only by the board
// build (main.cpp). The native test env excludes ble_peripheral_arduino.cpp,
// so it never compiles against ArduinoBLE.
// ---------------------------------------------------------------------------
class ArduinoBlePeripheral : public BlePeripheral {
public:
    bool begin(const char* deviceName) override;
    bool isConnected() const override;
    void notifySteps(uint32_t totalSteps) override;
    void onStepReset(StepResetCallback cb) override;
    void poll() override;

private:
    StepResetCallback resetCb = nullptr;
    bool begun = false; // true once BLE.begin() has succeeded

    // Cadence derivation state (driver-local; not part of the interface).
    uint32_t lastTotalSteps = 0;
    unsigned long lastNotifyMs = 0;
};

#endif // NANOWEAR_BLE_PERIPHERAL_ARDUINO_H
