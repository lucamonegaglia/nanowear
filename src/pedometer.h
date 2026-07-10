#ifndef NANOWEAR_PEDOMETER_H
#define NANOWEAR_PEDOMETER_H

#include <stdint.h>
#include "imu.h"

// ---------------------------------------------------------------------------
// Pedometer — hardware-step accumulator (pure logic)
// ---------------------------------------------------------------------------
// Consumes the absolute step count reported by the IMU's embedded pedometer
// and derives the running total plus the per-poll delta. It contains no I2C,
// no Serial and no millis(): all inputs are injected, so it is fully
// unit-testable on the host via MockIMU.
//
// The IMU pedometer is already cumulative and monotonic, so a reset drives the
// reported total back to zero. If the hardware counter is ever lower than the
// last seen total (e.g. after a reset the firmware didn't catch), the delta is
// clamped to zero rather than going negative.
// ---------------------------------------------------------------------------
class Pedometer {
public:
    explicit Pedometer(IMUSensor& imu) : imu(imu) {}

    // Reset cumulative state (e.g. right after the IMU's PEDO_RST_STEP command).
    void reset() { total = 0; }

    // Snapshot the current hardware counter and return the number of new steps
    // registered since the previous call.
    uint16_t update() {
        uint16_t hw = imu.readStepCount();
        uint16_t delta = (hw > total) ? static_cast<uint16_t>(hw - total) : 0;
        total = hw;
        return delta;
    }

    uint16_t getTotal() const { return total; }

private:
    IMUSensor& imu;
    uint16_t total = 0;
};

#endif // NANOWEAR_PEDOMETER_H
