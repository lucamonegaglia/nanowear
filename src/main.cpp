#include <Arduino.h>
#include "hardware_imu.h"
#include "pedometer.h"
#include "state_machine.h"

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

    // Machine-readable boot sentinel for the on-device e2e harness
    // (scripts/flash-verify.sh -> tests/e2e). The harness keys on this exact
    // line to confirm the firmware reached LOGGING; see tests/e2e/README.md.
    // It is a separate, stable token from the human "[STATE]" line so the
    // contract does not drift with wording changes.
    Serial.println("[NW] BOOT_OK");
}

void loop() {
    unsigned long now = millis();

    // Power-optimised, non-blocking poll: act only when LOGGING and the 2s
    // window has elapsed.
    if (tracker.shouldPoll(now)) {
        tracker.markPolled(now);

        uint16_t delta = pedometer.update();
        uint16_t total = pedometer.getTotal();

        // Re-emit the boot sentinel on the first LOGGING poll. The one in
        // setup() can be dropped if the serial monitor attaches after the
        // device has already booted (bytes printed before the host opens the
        // port are lost); this copy lands ~2s later, once a monitor is
        // connected, so the on-device e2e harness (tests/e2e) can rely on it.
        // See tests/e2e/README.md.
        static bool bootAnnounced = false;
        if (!bootAnnounced) {
            Serial.println("[NW] BOOT_OK");
            bootAnnounced = true;
        }

        Serial.print("[PEDOMETER] Total steps: ");
        Serial.println(total);

        if (delta > 0) {
            Serial.print("[PEDOMETER] New steps this poll: ");
            Serial.println(delta);
        }
        if (!pedometer.readOk()) {
            Serial.println("[PEDOMETER] Warning: step-count read failed (I2C error).");
        }
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
