#include <Arduino.h>
#include "hardware_imu.h"
#include "pedometer.h"
#include "elapsed_timer.h"
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

void setup() {
    Serial.begin(115200);

    // Standard Arduino Wire bus.
    Wire.begin();

    // Verify sensor presence via the high-level layer.
    if (!imu.begin()) {
        Serial.println("Critical Error: Failed to find LSM6DSOX.");
        while (1); // halt; no recovery path on this prototype
    }

    // Boot the hardware pedometer engine, then clear our accumulator.
    imu.initHardwarePedometer();
    pedometer.reset();

    // BOOT -> LOGGING.
    tracker.onBootComplete(millis());
    Serial.println("[STATE] BOOT complete -> LOGGING");
}

void loop() {
    unsigned long now = millis();

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
    }
}
