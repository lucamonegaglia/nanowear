#ifndef NANOWEAR_GAIT_METRICS_H
#define NANOWEAR_GAIT_METRICS_H

#include <stdint.h>

// ---------------------------------------------------------------------------
// GaitMetrics — one stride's worth of running dynamics
// ---------------------------------------------------------------------------
// Produced by GaitDetector whenever a stride completes. All timing fields
// are millisecond floats so sub-frame resolution from the high-ODR stream is
// preserved. See each field for what is MEASURED vs PROXIED:
//
//   MEASURED (directly from foot-strike / toe-off detection):
//     contactTimeMs, airTimeMs, swingTimeMs, stepTimeMs, cadenceSpm,
//     strike, footRecoveryProxy
//   PROXIED (inferred; improve with a second IMU / treadmill truth):
//     verticalOscillationMm, brakingIndex  (see gait_detector.cpp notes)
// ---------------------------------------------------------------------------
enum class StrikePattern : uint8_t {
    UNKNOWN = 0,   // not enough signal to classify
    REARFOOT = 1,  // heel strike  (foot dorsiflexed at contact)
    MIDFOOT  = 2,   // midfoot strike
    FOREFOOT = 3   // forefoot strike (foot plantarflexed at contact)
};

struct GaitMetrics {
    uint32_t timestampMs = 0;          // when this stride completed

    float contactTimeMs = 0;            // MEASURED: stance (foot-strike -> toe-off)
    float airTimeMs = 0;                // PROXIED: stepTime - contactTime
    float swingTimeMs = 0;              // MEASURED: toe-off -> next foot-strike
    float stepTimeMs = 0;               // MEASURED: foot-strike -> next foot-strike
    float cadenceSpm = 0;              // MEASURED: 60000 / stepTimeMs

    float verticalOscillationMm = 0;    // PROXIED: peak-to-peak CoM height (double-integrated)
    StrikePattern strike = StrikePattern::UNKNOWN; // MEASURED: foot pitch at contact

    float brakingIndex = 0;             // PROXIED: fore-aft decel impulse at contact (overstride proxy)
    float footRecoveryProxy = 0;         // MEASURED: peak |pitch rate| during swing (retraction speed)

    bool valid = false;                  // false until at least one full stride seen
};

#endif // NANOWEAR_GAIT_METRICS_H
