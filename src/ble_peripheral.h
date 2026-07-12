#ifndef NANOWEAR_BLE_PERIPHERAL_H
#define NANOWEAR_BLE_PERIPHERAL_H

#include <stdint.h>

// ---------------------------------------------------------------------------
// BlePeripheral — abstract view of the phone link
// ---------------------------------------------------------------------------
// Decouples the firmware's "connect to phone" logic from the concrete BLE
// radio. Every radio operation the app needs (advertise, report connection
// state, push step data, accept a reset command) goes through this interface,
// so the orchestration in main.cpp / loop() is unit-testable on the host using
// a fake implementation instead of a real NINA radio.
//
// This deliberately mirrors the project's IMUSensor / MockIMU seam: the
// production driver (ArduinoBlePeripheral, in ble_peripheral_arduino.cpp) is
// compiled only for the board, while the pure logic and the MockBlePeripheral
// test double below live here and run hardware-free under `pio test -e native`.
//
// Connection model (non-blocking, matches the rest of the firmware):
//   * begin() starts advertising once; thereafter loop() must call poll() every
//     tick to pump the radio. No delay() anywhere.
//   * A phone (central) connects/disconnects asynchronously; isConnected()
//     reflects the current state for status LED / sync gating.
//   * notifySteps() pushes the latest cumulative step count to subscribed
//     centrals (BLE Notify). It never blocks on a connection.
//   * onStepReset() registers the callback the driver invokes when a central
//     asks the device to zero its step count.
// ---------------------------------------------------------------------------

// Callback invoked when a connected phone requests a step reset. Kept as a
// plain C-style function pointer (not std::function) so it works unchanged on
// the constrained MCU and in the host test env.
using StepResetCallback = void (*)();

class BlePeripheral {
public:
    virtual ~BlePeripheral() = default;

    // Start advertising under the given device name. Returns true on success.
    virtual bool begin(const char* deviceName) = 0;

    // True while a central (phone) is connected.
    virtual bool isConnected() const = 0;

    // Push the latest cumulative step count to subscribed centrals (Notify).
    // Safe to call when no one is connected — the radio simply drops it.
    virtual void notifySteps(uint32_t totalSteps) = 0;

    // Register the callback fired when a central requests a step reset.
    virtual void onStepReset(StepResetCallback cb) = 0;

    // Non-blocking radio upkeep; call once per loop() tick. Pumps BLE events
    // (incoming connections, control-point writes, advertising).
    virtual void poll() = 0;
};

// ---------------------------------------------------------------------------
// MockBlePeripheral — test double for the host (native) unit tests
// ---------------------------------------------------------------------------
// Records every call and exposes scriptable state. Tests assert on the call
// counters / last values and drive connection state via the public `connected`
// flag. No radio, no ArduinoBLE, no hardware is touched. A test helper
// (simulateResetRequest) invokes the registered callback the same way the real
// driver would when a control-point write arrives.
// ---------------------------------------------------------------------------
class MockBlePeripheral : public BlePeripheral {
public:
    bool beginCalled = false;
    bool beginResult = true;
    bool connected = false;
    uint32_t lastNotifiedSteps = 0;
    int notifyCallCount = 0;
    StepResetCallback resetCb = nullptr;
    int resetRequestsHandled = 0;
    const char* beginName = nullptr;

    bool begin(const char* deviceName) override {
        beginCalled = true;
        beginName = deviceName;
        return beginResult;
    }

    bool isConnected() const override { return connected; }

    void notifySteps(uint32_t totalSteps) override {
        lastNotifiedSteps = totalSteps;
        notifyCallCount++;
    }

    void onStepReset(StepResetCallback cb) override { resetCb = cb; }

    void poll() override {
        // No radio to pump in the mock.
    }

    // Test helper: behave as if a central sent a reset command, firing the
    // registered callback exactly as the real driver would.
    void simulateResetRequest() {
        if (resetCb) {
            resetCb();
            resetRequestsHandled++;
        }
    }
};

#endif // NANOWEAR_BLE_PERIPHERAL_H
