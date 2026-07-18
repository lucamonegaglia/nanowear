#ifndef NANOWEAR_STEP_DETECTOR_H
#define NANOWEAR_STEP_DETECTOR_H

#include <stdint.h>
#include "step_source.h"   // StepSource (the swappable step-count seam)
#include "imu_fifo.h"      // ImuSample, SampleConsumer

// ---------------------------------------------------------------------------
// SoftwareStepDetector — pure, host-testable software step counter
// ---------------------------------------------------------------------------
// Replaces the unreliable LSM6DSOX embedded (MLC) pedometer. Consumes decoded
// ImuSamples through SampleConsumer::onSample() and detects steps with an
// adaptive threshold on the squared acceleration magnitude. It is a StepSource,
// so Pedometer consumes it exactly as it consumed the old hardware pedometer —
// flipping the active source in main.cpp is a one-line change.
//
// Design notes (KISS, RP2040-friendly):
//   * Works in magnitude-squared space (no sqrt), so no FPU pressure.
//   * Baseline energy and oscillation amplitude are tracked with two EMAs; the
//     step threshold is baseline + prominence * amplitude, floored so quiet
//     motion never triggers.
//   * A rising-edge test + debounce window (~250 ms) yield one step per foot
//     strike and cap the rate near 240 spm.
//   * No I2C, no millis(); all input arrives via onSample(), so it is fully
//     unit-testable on the host with synthetic signals (test/test_step_detector).
// ---------------------------------------------------------------------------
class SoftwareStepDetector : public StepSource, public SampleConsumer {
public:
    SoftwareStepDetector() {}

    // StepSource: report the absolute cumulative step count.
    bool read(uint16_t& out) override;

    // StepSource: zero the internal counter (pairs with Pedometer::reset()).
    void reset() override;

    // SampleConsumer: ingest one decoded sample and run detection.
    void onSample(const ImuSample& s) override;

    // Instantaneous cadence (steps/min) from recent step intervals. 0 until at
    // least two steps have been seen.
    uint16_t getCadenceSpm() const;

    const char* implName() const { return "software-detector"; }

private:
    static constexpr float kAlphaMean  = 0.001f;  // baseline EMA (slow)
    static constexpr float kAlphaDev   = 0.005f;  // amplitude EMA
    static constexpr float kProminence = 2.0f;    // threshold = mean + k*amp
    static constexpr float kMinDev     = 0.2f;    // mag^2 floor (rejects rest)
    static constexpr uint32_t kDebounceMs = 250;  // ~240 spm ceiling
    static constexpr size_t kIntervals = 8;        // cadence ring buffer

    float    meanMag2_ = 0;                 // EMA of |a|^2 (baseline energy)
    float    devMag2_  = 0;                 // EMA of |a - mean| (amplitude)
    uint32_t lastStepTs_ = 0;               // ts of last step (ms)
    bool     prevAbove_  = false;           // rising-edge memory
    uint16_t total_ = 0;                    // cumulative steps (saturates 65535)

    uint32_t intervals_[kIntervals] = {0}; // recent step-to-step intervals (ms)
    uint8_t  intervalIdx_   = 0;
    uint8_t  intervalCount_ = 0;
};

#endif // NANOWEAR_STEP_DETECTOR_H
