#ifndef NANOWEAR_STEP_SOURCE_H
#define NANOWEAR_STEP_SOURCE_H

#include <stdint.h>
#include "imu.h"   // IMUSensor (HardwareStepSource wraps it)

// ---------------------------------------------------------------------------
// StepSource — the swappable step-count seam (the "inference layer")
// ---------------------------------------------------------------------------
// The pedometer's step COUNT can come from several implementations, and the
// firmware flips between them with a one-line change in main.cpp:
//   * SoftwareStepDetector (src/step_detector.h) — ACTIVE source. A custom
//     software algorithm on the RP2040 that detects steps from the raw FIFO
//     stream; the embedded (MLC) pedometer proved unreliable on hardware, so
//     this replaces it (see AGENTS.md / step_detector.h).
//   * HardwareStepSource — the LSM6DSOX embedded (MLC) pedometer via IMUSensor.
//     Selectable via NANOWEAR_MLC_PEDOMETER if we ever revisit that route.
//   * SoftwareStepSource — counts foot-strike events the GaitDetector already
//     produces from the raw stream; an alternative software path.
//
// Pedometer depends ONLY on this interface (read() = cumulative count, reset()
// = zero it), so any implementation can be swapped without touching the
// accumulator, BLE, or debug output. This seam is what keeps the step-counting
// strategy cheap to change.
// ---------------------------------------------------------------------------
class StepSource {
public:
    virtual ~StepSource() = default;

    // Read the absolute cumulative step count. Returns false on a transport
    // error; in that case `out` is left unchanged.
    virtual bool read(uint16_t& out) = 0;

    // Zero the source's counter. Pairs with Pedometer::reset(); for the
    // software detector this zeroes its internal total, for the hardware source
    // it pulses the device's reset. Called on a user/phone reset request.
    virtual void reset() = 0;
};

// Wraps the LSM6DSOX embedded pedometer via IMUSensor. Optional legacy source
// (NANOWEAR_MLC_PEDOMETER).
class HardwareStepSource : public StepSource {
public:
    explicit HardwareStepSource(IMUSensor& imu) : imu(imu) {}

    bool read(uint16_t& out) override { return imu.readStepCount(out); }
    void reset() override { imu.resetStepCount(); }

private:
    IMUSensor& imu;
};

// Counts foot-strike events as steps. Self-contained counter; main.cpp calls
// onStride() from the GaitDetector stride callback when that path is selected.
// Saturates at 65535 like the hardware counter.
class SoftwareStepSource : public StepSource {
public:
    void onStride() { if (count < 65535) count++; }  // placeholder hook

    bool read(uint16_t& out) override { out = count; return true; }
    void reset() override { count = 0; }

private:
    uint16_t count = 0;
};

#endif // NANOWEAR_STEP_SOURCE_H
