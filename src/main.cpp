#include <Arduino.h>
#include "hardware_imu.h"
#include "pedometer.h"
#include "elapsed_timer.h"

// ---------------------------------------------------------------------------
// NanoWear firmware — screenless ankle-worn fitness tracker
// ---------------------------------------------------------------------------
// Architecture notes (see CLAUDE.md / AGENTS.md):
//   * Non-blocking: loop() never calls delay(); polling is driven by an
//     ElapsedTimer so the MCU stays free for other work / sleep.
//   * Testable: step logic lives in Pedometer (pure) behind the IMU interface;
//     the board-only HardwareIMU is the only part that touches I2C.
// ---------------------------------------------------------------------------

static HardwareIMU imu;
static Pedometer pedometer(imu);
static ElapsedTimer pollTimer(2000); // poll the isolated HW counter every 2s

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
    pollTimer.reset(millis());
}

void loop() {
    unsigned long now = millis();

    // Power-optimised poll: act only when the 2s window has elapsed.
    if (pollTimer.hasElapsed(now)) {
        pollTimer.reset(now);

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
