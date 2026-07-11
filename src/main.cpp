#include <Arduino.h>
#include <WiFiNINA.h>            // onboard RGB LED (LEDR/LEDG/LEDB); active-low
#include "hardware_imu.h"
#include "pedometer.h"
#include "state_machine.h"
#include "ble_peripheral.h"
#include "ble_peripheral_arduino.h"  // board-only concrete BLE driver

// ---------------------------------------------------------------------------
// NanoWear firmware — screenless ankle-worn fitness tracker
// ---------------------------------------------------------------------------
// Architecture (see CLAUDE.md / AGENTS.md / ROADMAP.md):
//   * Non-blocking: loop() never calls delay(); polling is driven by the
//     StateMachine + ElapsedTimer so the MCU stays free for sleep / BLE.
//   * Testable: step logic lives in Pedometer (pure) behind the IMU interface;
//     the board-only HardwareIMU is the only part that touches I2C. The
//     StateMachine is also pure, so the orchestration is host-testable.
//   * Current scope (just the board, USB-powered): BOOT -> LOGGING. SYNC and
//     LOW_BATTERY are wired but entered only by future BLE / power code.
// ---------------------------------------------------------------------------

static HardwareIMU imu;
static Pedometer pedometer(imu);
static StateMachine tracker(2000); // poll the isolated HW counter every 2s while LOGGING
static ArduinoBlePeripheral ble;   // phone link (NINA BLE, RSC + custom service)

// Reset handler invoked by the phone over BLE: zero the hardware pedometer and
// our accumulator. Declared as a plain function so it matches the C-style
// StepResetCallback the driver expects (no captures needed — it uses the
// module globals above).
void handleStepReset() {
    imu.resetPedometerSteps();
    pedometer.reset();
    Serial.println("[BLE] Step reset requested by phone");
}

// Throttle for the "not logging" diagnostic so it doesn't flood Serial.
static unsigned long lastStateDiagMs = 0;

void setup() {
    Serial.begin(115200);

    // Standard Arduino Wire bus.
    Wire.begin();

    // Verify sensor presence + configure the embedded pedometer engine.
    if (!imu.begin()) {
        // No recovery path on this prototype: stay in BOOT so loop() surfaces a
        // periodic diagnostic instead of silently halting the MCU.
        Serial.println("Critical Error: Failed to find or configure LSM6DSOX.");
        return;
    }

    // BOOT -> LOGGING. pedometer.reset() pairs with the PEDO_RST_STEP command
    // issued inside imu.begin(), zeroing the firmware accumulator to match.
    pedometer.reset();
    tracker.startLogging(millis());
    Serial.println("[STATE] BOOT complete -> LOGGING");

    // Report the NINA module firmware version. BLE over the NINA-W102 requires
    // firmware >= 3.0.1 (see docs/BLE_SETUP.md); older versions make BLE.begin()
    // fail and must be flashed via the Arduino IDE / esptool before the radio
    // works. Printing it here makes the gate visible from the serial monitor.
    Serial.print("[BLE] NINA firmware: ");
    Serial.println(WiFi.firmwareVersion());

    // Start the BLE peripheral (advertise as "NanoWear"). The RGB LED is the
    // at-a-glance status: blue = advertising/discoverable, off = connected.
    pinMode(LEDB, OUTPUT);
    if (!ble.begin("NanoWear")) {
        Serial.println("[BLE] ERROR: failed to start peripheral (NINA firmware < 3.0.1?)");
    } else {
        ble.onStepReset(handleStepReset);
        digitalWrite(LEDB, LOW); // blue ON = advertising
        Serial.println("[BLE] Advertising as 'NanoWear' (RSC 0x1814 + NanoWear steps)");
    }
}

void loop() {
    unsigned long now = millis();

    // Pump the BLE radio every tick (non-blocking). This services incoming
    // connections and any reset command the phone may have written.
    ble.poll();

    // Reflect connection state on the status LED: blue while advertising, off
    // once a phone is connected. (Green could be used for "connected" instead.)
    digitalWrite(LEDB, ble.isConnected() ? HIGH : LOW);

    // Power-optimised, non-blocking poll: act only when LOGGING and the 2s
    // window has elapsed.
    if (tracker.shouldPoll(now)) {
        tracker.markPolled(now);

        uint16_t delta = pedometer.update();
        uint16_t total = pedometer.getTotal();

        Serial.print("[PEDOMETER] Total steps: ");
        Serial.println(total);

        if (delta > 0) {
            Serial.print("[PEDOMETER] New steps this poll: ");
            Serial.println(delta);
        }
        if (!pedometer.readOk()) {
            Serial.println("[PEDOMETER] Warning: step-count read failed (I2C error).");
        }

        // Push the latest total to any connected phone (Notify + custom char).
        ble.notifySteps(total);
    } else if (tracker.state() != TrackerState::LOGGING
               && now - lastStateDiagMs >= 5000) {
        // We are paused outside LOGGING (e.g. BOOT after a sensor failure, or a
        // future SYNC/LOW_BATTERY state). Surface it so a stuck state is visible
        // rather than silently dropping steps.
        lastStateDiagMs = now;
        Serial.print("[STATE] Not logging (");
        Serial.print(static_cast<int>(tracker.state()));
        Serial.println("); steps are not being recorded.");
    }
}
