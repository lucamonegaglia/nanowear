#ifndef NANOWEAR_STEP_SOURCE_H
#define NANOWEAR_STEP_SOURCE_H

#include <stdint.h>
#include "imu.h"   // IMUSensor (HardwareStepSource wraps it)

// ---------------------------------------------------------------------------
// StepSource — the swappable step-count seam
// ---------------------------------------------------------------------------
// The pedometer's step COUNT can come from two places:
//   * HardwareStepSource — the LSM6DSOX embedded (MLC) pedometer, reached
//     through the IMUSensor interface. This is the ACTIVE source today and
//     preserves the "set in stone" step-counting rule from AGENTS.md.
//   * SoftwareStepSource — counts foot-strike events the GaitDetector already
//     produces from the raw stream. NOT wired in yet; it is the "full
//     software pedometer" path, selectable later via a compile flag in
//     main.cpp (NANOWEAR_SOFTWARE_PEDOMETER) with NO detector rewrite.
//
// Pedometer depends only on this interface, so flipping the source is a
// one-line change in main.cpp. (See AGENTS.md note: the constraint can be
// relaxed later; this seam is what makes that cheap.)
// ---------------------------------------------------------------------------
class StepSource {
public:
    virtual ~StepSource() = default;

    // Read the absolute cumulative step count. Returns false on a transport
    // error; in that case `out` is left unchanged.
    virtual bool read(uint16_t& out) = 0;
};

// Wraps the LSM6DSOX embedded pedometer via IMUSensor. Active source.
class HardwareStepSource : public StepSource {
public:
    explicit HardwareStepSource(IMUSensor& imu) : imu(imu) {}

    bool read(uint16_t& out) override { return imu.readStepCount(out); }

private:
    IMUSensor& imu;
};

// Counts foot-strike events as steps. Self-contained counter; main.cpp will
// call onStride() from the GaitDetector stride callback once the software
// path is selected. Saturates at 65535 like the hardware counter today.
class SoftwareStepSource : public StepSource {
public:
    void onStride() { if (count < 65535) count++; }

    bool read(uint16_t& out) override { out = count; return true; }

private:
    uint16_t count = 0;
};

#endif // NANOWEAR_STEP_SOURCE_H
