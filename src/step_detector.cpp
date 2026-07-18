#include "step_detector.h"
#include <math.h>   // fabsf

// ---------------------------------------------------------------------------
// SoftwareStepDetector — adaptive-threshold step detection
// ---------------------------------------------------------------------------
// See step_detector.h for the algorithm and design rationale. All state is
// private; the only entry point is onSample() (fed by the unified FIFO sampler)
// and the StepSource accessors read()/getCadenceSpm().
// ---------------------------------------------------------------------------

bool SoftwareStepDetector::read(uint16_t& out) {
    out = total_;
    return true;
}

void SoftwareStepDetector::reset() {
    total_ = 0;
    meanMag2_ = 0;
    devMag2_  = 0;
    lastStepTs_ = 0;
    prevAbove_  = false;
    intervalIdx_   = 0;
    intervalCount_ = 0;
}

uint16_t SoftwareStepDetector::getCadenceSpm() const {
    if (intervalCount_ < 2) return 0;
    uint32_t sum = 0;
    for (uint8_t i = 0; i < intervalCount_; i++) sum += intervals_[i];
    uint32_t avg = sum / intervalCount_;   // mean step interval, ms
    if (avg == 0) return 0;
    return static_cast<uint16_t>(60000u / avg);
}

void SoftwareStepDetector::onSample(const ImuSample& s) {
    // Squared acceleration magnitude (avoids sqrt; threshold lives in mag^2).
    const float mag2 = s.ax * s.ax + s.ay * s.ay + s.az * s.az;

    // Bootstrap the EMAs on the first sample so the threshold is well-defined.
    if (meanMag2_ == 0.0f) {
        meanMag2_ = mag2;
        devMag2_  = 0.0f;
        prevAbove_ = false;
        return;
    }

    const float dev = mag2 - meanMag2_;                 // signed deviation
    const float amp = (devMag2_ > kMinDev) ? devMag2_ : kMinDev;
    const bool above = (dev > kProminence * amp) && (dev > 0.0f);

    // One step per rising edge, gated by the debounce window.
    const bool debounced = (lastStepTs_ == 0) ||
                           (s.ts - lastStepTs_ >= kDebounceMs);
    if (above && !prevAbove_ && debounced) {
        if (total_ < 65535) total_++;
        if (lastStepTs_ != 0) {
            intervals_[intervalIdx_] = s.ts - lastStepTs_;
            intervalIdx_ = (intervalIdx_ + 1) % kIntervals;
            if (intervalCount_ < kIntervals) intervalCount_++;
        }
        lastStepTs_ = s.ts;
    }
    prevAbove_ = above;

    // Update the EMAs AFTER evaluation so a single step does not immediately
    // lift the baseline. Slow mean + faster amplitude keeps the threshold
    // stable through the step but adaptive to posture changes.
    meanMag2_ += kAlphaMean * dev;
    devMag2_  += kAlphaDev * (fabsf(dev) - devMag2_);
}
