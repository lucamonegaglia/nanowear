#ifndef NANOWEAR_GAIT_DETECTOR_H
#define NANOWEAR_GAIT_DETECTOR_H

#include <stdint.h>
#include "imu_fifo.h"
#include "gait_metrics.h"

// ---------------------------------------------------------------------------
// GaitDetector — running-dynamics estimator from a foot/ankle IMU stream
// ---------------------------------------------------------------------------
// Consumes a stream of decoded ImuSamples (see imu_fifo.h) and, each time a
// stride completes, populates a GaitMetrics. It is PURE logic: no I2C, no
// millis(), no float-heavy stdlib — so it is fully unit-testable on the host
// with synthetic signals (test/test_gait.cpp). The board feeds it batches of
// FIFO-decoded samples from Core1.
//
// WHAT IT COMPUTES (single ankle IMU — see exploration notes):
//   * Contact time / stance      — interval between foot-strike (IC) and
//                                     toe-off (TC), detected as the two
//                                     minima of pitch angular velocity.
//   * Swing / step time, cadence — from the IC->IC stride period.
//   * Air time (proxy)         — stepTime - contactTime.
//   * Strike pattern            — foot pitch angle at IC (heel/mid/forefoot).
//   * Foot-recovery proxy       — peak |pitch rate| during swing.
//   * Braking / overstride     — fore-aft decel impulse at IC (PROXY only;
//     (proxy)                     true overstride distance needs a hip sensor).
//   * Vertical oscillation       — peak-to-peak CoM height from double-integrated
//     (proxy)                     vertical accel (PROXY; needs calibration).
//
// MOUNTING-AGNOSTIC: the pitch (sagittal) axis is auto-selected at boot as
// the gyro axis with the largest variance, so the pod works regardless of how
// it is taped to the ankle/foot. Gravity is estimated from a short stationary
// window for the vertical-reference and the strike classification.
// ---------------------------------------------------------------------------
class GaitDetector {
public:
    // `sampleHz` is the FIFO ODR (default 1.66 kHz); it sets the Butterworth
    // cut-off scaling and the fall-back dt when a sample lacks a useful ts.
    explicit GaitDetector(float sampleHz = 1660.f) : sampleHz(sampleHz) {
        configureFilters(sampleHz);
    }

    // Feed one decoded sample. Returns true if a stride just completed (the
    // metrics() are now fresh and hasNewMetrics() is set).
    bool process(const ImuSample& s);

    const GaitMetrics& metrics() const { return m; }
    bool hasNewMetrics() const { return newMetrics; }
    void clearNewMetrics() { newMetrics = false; }

    // True once calibration (first kCalSamples) has finished, so the board can
    // stop holding still.
    bool isCalibrated() const { return calibrated; }

private:
    // --- calibration -----------------------------------------------------
    static constexpr size_t kCalSamples = 64;   // ~40ms at 1.66kHz
    size_t calCount = 0;
    float calGx = 0, calGy = 0, calGz = 0;  // gyro accumulation
    float calGx2 = 0, calGy2 = 0, calGz2 = 0;
    float calAx = 0, calAy = 0, calAz = 0;  // accel accumulation (gravity)
    bool calibrated = false;
    int pitchAxis = 1;                          // 0=x,1=y,2=z (chosen in motion)
    float gWorldUp[3] = {0, 0, 1};          // +up direction in sensor frame

    // MOUNTING-AGNOSTIC pitch axis: chosen from the gyro axis with the largest
    // variance over the FIRST kAxisSamples of MOTION (post-calibration), not
    // during the stillness window — so it tracks the true sagittal axis, not
    // sensor noise.
    static constexpr size_t kAxisSamples = 32;  // ~19ms @1.66kHz of running
    float axisG[3] = {0, 0, 0};
    float axisG2[3] = {0, 0, 0};
    size_t axisN = 0;
    bool axisChosen = false;

    void calibrate(const ImuSample& s);

    // Sample rate (Hz) the FIFO is configured for; used for filter scaling
    // and as the dt fall-back. Set from the constructor.
    float sampleHz = 1660.f;

    // --- sample-ring / counting state ----------------------------------
    uint32_t lastTs = 0;
    uint32_t nSeen = 0;     // samples since calibration
    uint32_t minCount = 0;   // alternating IC/TC minima counter

    // 3-sample sliding window on the low-passed pitch rate, plus the
    // accompanying data of the MIDDLE sample (where a minimum is decided).
    float rPitch[3] = {0, 0, 0};
    uint32_t rTs[3] = {0, 0, 0};
    float rPitchAng[3] = {0, 0, 0};
    float rH[3] = {0, 0, 0};

    // --- filtering (2nd-order Butterworth low-pass, fc ~30 Hz) -----------
    // One biquad per signal we smooth; kept tiny and allocation-free.
    struct Biquad {
        float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0; // coeffs
        float z1 = 0, z2 = 0;   // input history
        float y1 = 0, y2 = 0;   // output history
        float process(float x);
    };
    Biquad lpPitch;     // pitch-rate low-pass (also smoothes the strike angle)
    void configureFilters(float sampleHz);

    // --- gait phase state machine -----------------------------------------
    // We track the last two pitch-rate minima. The first of a pair (after a
    // swing) is the foot-strike (IC); the second is the toe-off (TC). The
    // IC->TC gap is contact time; TC->next IC is swing; IC->next IC is the
    // stride (step) time.
    enum class Phase { UNKNOWN, STANCE, SWING };
    Phase phase = Phase::UNKNOWN;

    uint32_t lastMinTs = 0;     // timestamp of the most recent pitch min
    bool haveMin = false;
    uint32_t icTs = 0;           // most recent foot-strike
    uint32_t tcTs = 0;           // most recent toe-off
    uint32_t prevIcTs = 0;       // previous foot-strike (for stride period)
    bool haveIc = false;     // previous foot-strike present (also == havePrevIc)
    bool haveTc = false;

    // Pitch-angle estimate (integrated pitch rate, detrended at each IC so it
    // does not wind up). Used for strike-pattern classification at IC.
    float pitchAngle = 0;          // deg, relative to mid-stance baseline
    float pitchAtIc = 0;          // captured at the moment of IC

    // Swing peak |pitch rate| (foot-recovery proxy).
    float swingPeakRate = 0;

    // Vertical-oscillation integration accumulator (detrended per stride).
    float veloZ = 0;             // integrated vertical velocity, m/s
    float posZ = 0;               // integrated height, m
    float posZmin = 0, posZmax = 0;
    bool strideActive = false;

    // Fore-aft braking accumulator during early stance.
    float brakeAccum = 0;
    uint32_t brakeWindowEnd = 0;
    bool braking = false;

    // Plausibility gates (reject spurious minima / sensor glitches).
    static constexpr float kMinContactMs = 60.f;   // shortest believable stance
    static constexpr float kMaxContactMs = 700.f;  // longest believable stance
    static constexpr float kMinStepMs    = 180.f;  // ~333 spm ceiling
    static constexpr float kMaxStepMs     = 2000.f;  // ~30 spm floor
    static constexpr float kMinMinGapMs   = 80.f;   // de-bounce between minima

    // Detect a local minimum of the (low-passed) pitch rate and, if plausible,
    // advance the phase machine. Returns true if a stride completed.
    bool handlePitchMin(uint32_t ts, float pitchRate, float pitchNow,
                        float hMag);

    void resetStrideWindow(uint32_t ts);

    float gToMs2(float g) const { return g * 9.80665f; }

    GaitMetrics m;
    bool newMetrics = false;
};

#endif // NANOWEAR_GAIT_DETECTOR_H
