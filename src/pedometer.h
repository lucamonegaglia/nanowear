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
//
// Transport failures are handled gracefully: if readStepCount() reports an
// error, the running `total` is left untouched (no steps are lost or invented)
// and update() returns 0. Call readOk() after update() to see whether the last
// read succeeded.
// ---------------------------------------------------------------------------
class Pedometer {
public:
    explicit Pedometer(IMUSensor& imu) : imu(imu) {}

    // Reset cumulative state to zero. MUST be called immediately after the
    // hardware counter is reset (e.g. the IMU's PEDO_RST_STEP command issued in
    // HardwareIMU::begin()); calling it alone, while the hardware keeps counting,
    // makes the next update() report a bogus delta equal to the whole hardware
    // total.
    void reset() { total = 0; }

    // Snapshot the current hardware counter and return the number of new steps
    // registered since the previous call. Returns 0 if the sensor could not be
    // read (transport error); the running total is preserved in that case.
    uint16_t update() {
        uint16_t hw = 0;
        lastReadOk_ = imu.readStepCount(hw);
        if (!lastReadOk_) return 0;
        uint16_t delta = (hw > total) ? static_cast<uint16_t>(hw - total) : 0;
        total = hw;
        return delta;
    }

    uint16_t getTotal() const { return total; }

    // True if the most recent update() successfully read the sensor.
    bool readOk() const { return lastReadOk_; }

private:
    IMUSensor& imu;
    uint16_t total = 0;
    bool lastReadOk_ = false;
};

#endif // NANOWEAR_PEDOMETER_H
