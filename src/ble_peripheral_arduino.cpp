#include "ble_peripheral_arduino.h"
#include "rsc_codec.h"
#include <ArduinoBLE.h>

// ---------------------------------------------------------------------------
// ArduinoBlePeripheral — implementation
// ---------------------------------------------------------------------------
// Static state is used for the BLE event callbacks because ArduinoBLE's
// setEventHandler / setEventHandler(BLEWritten) accept plain C function
// pointers (no capturing lambdas). There is exactly one peripheral instance on
// the board, so static forwarding is safe.
// ---------------------------------------------------------------------------

static bool s_connected = false;
static StepResetCallback s_resetCb = nullptr;

// Vendor UUIDs for the custom NanoWear service. PLACEHOLDER: replace with a
// UUID registered to you before shipping so it does not collide with others.
static const char* NANOWEAR_SERVICE_UUID = "9a1b2c3d-0000-4b06-a1b2-3c4d5e6f7a8b";
static const char* NANOWEAR_STEPS_UUID   = "9a1b2c3d-0001-4b06-a1b2-3c4d5e6f7a8b";
static const char* NANOWEAR_CONTROL_UUID = "9a1b2c3d-0002-4b06-a1b2-3c4d5e6f7a8b";
static const char* NANOWEAR_DYNAMICS_UUID = "9a1b2c3d-0003-4b06-a1b2-3c4d5e6f7a8b";

// GATT objects (one set; reused across connections).
static BLEService        rscService("1814");
static BLECharacteristic rscMeasurement("2A53", BLERead | BLENotify, 4);
static BLEUnsignedShortCharacteristic rscFeature("2A54", BLERead);
static BLEUnsignedCharCharacteristic  sensorLocation("2A5D", BLERead);
static BLEService        nanoService(NANOWEAR_SERVICE_UUID);
static BLECharacteristic nanoSteps(NANOWEAR_STEPS_UUID, BLERead | BLENotify, 4);
static BLECharacteristic nanoControl(NANOWEAR_CONTROL_UUID, BLEWrite, 1);
static BLECharacteristic nanoDynamics(NANOWEAR_DYNAMICS_UUID, BLERead | BLENotify, 10);

// RSC Feature bitmask (uint16): bit0 stride-length, bit1 total-distance,
// bit2 walking/running, bit3 calibration, bit4 multi-sensor-location.
// We support cadence only -> 0.
static constexpr uint16_t RSC_FEATURE_VALUE = 0x0000;

// ---- static BLE event handlers ---------------------------------------------

static void onBleConnected(BLEDevice /*central*/) {
    s_connected = true;
}

static void onBleDisconnected(BLEDevice /*central*/) {
    s_connected = false;
}

// Fired when a central writes to the Control characteristic. Value 0x01 = reset.
static void onControlWritten(BLEDevice /*central*/, BLECharacteristic ch) {
    if (ch.valueLength() >= 1 && ch.value()[0] == 0x01 && s_resetCb) {
        s_resetCb();
    }
}

// ---- BlePeripheral interface -----------------------------------------------

bool ArduinoBlePeripheral::begin(const char* deviceName) {
    if (!BLE.begin()) {
        return false;
    }

    BLE.setDeviceName(deviceName);
    BLE.setLocalName(deviceName);

    // RSC service + its three characteristics.
    BLE.setAdvertisedService(rscService);
    rscService.addCharacteristic(rscMeasurement);
    rscService.addCharacteristic(rscFeature);
    rscService.addCharacteristic(sensorLocation);
    BLE.addService(rscService);
    rscFeature.writeValue(RSC_FEATURE_VALUE);
    sensorLocation.writeValue(RSC_SENSOR_LOCATION_FOOT);

    // Custom NanoWear service: raw steps + reset control + dynamics.
    nanoService.addCharacteristic(nanoSteps);
    nanoService.addCharacteristic(nanoControl);
    nanoService.addCharacteristic(nanoDynamics);
    BLE.addService(nanoService);

    // Wire events.
    BLE.setEventHandler(BLEConnected, onBleConnected);
    BLE.setEventHandler(BLEDisconnected, onBleDisconnected);
    nanoControl.setEventHandler(BLEWritten, onControlWritten);

    BLE.advertise();
    begun = true;
    return true;
}

bool ArduinoBlePeripheral::isConnected() const {
    return s_connected;
}

void ArduinoBlePeripheral::notifySteps(uint32_t totalSteps) {
    if (!begun) return; // nothing to notify on until BLE is up

    // Derive cadence (steps/min) from the step delta since the last push.
    // The cadence maths is a pure host-testable helper (see rsc_codec.h).
    unsigned long now = millis();
    uint32_t dtMs = (lastNotifyMs != 0 && now > lastNotifyMs)
                        ? static_cast<uint32_t>(now - lastNotifyMs)
                        : 0;
    uint32_t delta = (totalSteps > lastTotalSteps)
                         ? (totalSteps - lastTotalSteps)
                         : 0;
    uint16_t cadenceSpm = deriveCadenceSpm(delta, dtMs);
    lastTotalSteps = totalSteps;
    lastNotifyMs = now;

    // Skip the radio churn when no central is connected; we still keep the
    // cadence bookkeeping above current so the first notify after connect is
    // correct and we don't replay a huge artificial starting delta.
    if (!s_connected) return;

    // RSC Measurement: flags=0 (cadence only), speed=0 (unavailable).
    uint8_t rsc[4];
    encodeRscMeasurement(rsc, rscFlags(false, false, false, false),
                         cadenceToRscUnits(cadenceSpm));
    rscMeasurement.writeValue(rsc, 4);

    // Custom Steps characteristic: authoritative cumulative step count.
    uint8_t steps[4];
    encodeStepCount(steps, totalSteps);
    nanoSteps.writeValue(steps, 4);
}

void ArduinoBlePeripheral::notifyGait(const GaitMetrics& m) {
    if (!begun) return; // nothing to notify on until BLE is up
    // Pack the one-stride snapshot into the 10-byte dynamics payload.
    uint8_t buf[10];
    encodeGaitMetrics(buf, m);
    nanoDynamics.writeValue(buf, 10);
}

void ArduinoBlePeripheral::onStepReset(StepResetCallback cb) {
    s_resetCb = cb;
}

void ArduinoBlePeripheral::poll() {
    if (!begun) return; // safe no-op until BLE.begin() has succeeded
    BLE.poll(); // non-blocking; pumps connections + incoming writes
}
