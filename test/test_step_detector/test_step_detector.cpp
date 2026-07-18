#include <unity.h>
#include "step_detector.h"
#include "imu_fifo.h"   // ImuSample

// ---------------------------------------------------------------------------
// SoftwareStepDetector — native unit tests
// ---------------------------------------------------------------------------
// Drives the detector directly through SampleConsumer::onSample() (no IMU, no
// I2C) with a synthetic acceleration-magnitude signal: a quiet baseline
// (~1 g) punctuated by periodic "step" spikes (~2.6 g). We set ax = sqrt(mag2)
// with ay=az=0 so the squared magnitude equals the intended value exactly.
// ---------------------------------------------------------------------------

// Feed `nPulses` spikes, one every `periodMs`. Real steps are brief impacts
// (low duty cycle) on a ~1 g baseline, so the adaptive baseline stays near
// rest and each spike stands out. We feed a long quiet baseline followed by a
// short high-energy spike per period, as a continuous stream (no gaps).
static void feedPulses(SoftwareStepDetector& det, uint32_t& ts,
                       uint32_t startMs, uint32_t periodMs, uint32_t nPulses,
                       uint32_t sampleMs = 2) {
    const uint32_t kSpikeMs = 30;
    for (uint32_t i = 0; i < nPulses; i++) {
        uint32_t p = startMs + i * periodMs;
        for (uint32_t t = p; t < p + periodMs - kSpikeMs; t += sampleMs, ts = t) {
            ImuSample s; s.ax = 1.0f; s.ay = 0; s.az = 0; s.ts = ts;
            det.onSample(s);
        }
        for (uint32_t t = p + periodMs - kSpikeMs; t < p + periodMs; t += sampleMs, ts = t) {
            ImuSample s; s.ax = 2.6f; s.ay = 0; s.az = 0; s.ts = ts;
            det.onSample(s);
        }
    }
}

static void test_stationary_yields_zero(void) {
    SoftwareStepDetector det;
    uint32_t ts = 0;
    for (uint32_t t = 0; t < 5000; t += 5, ts = t) {
        ImuSample s; s.ax = 1.0f; s.ay = 0; s.az = 0; s.ts = ts;
        det.onSample(s);
    }
    uint16_t total = 0;
    det.read(total);
    TEST_ASSERT_EQUAL_UINT16(0, total);
}

static void test_detects_known_step_count(void) {
    SoftwareStepDetector det;
    uint32_t ts = 0;
    feedPulses(det, ts, 100, 500, 20);   // 20 steps every 500 ms (~120 spm)
    uint16_t total = 0;
    det.read(total);
    TEST_ASSERT_EQUAL_UINT16(20, total);

    // Cadence should land near 120 spm (intervals ~500 ms).
    uint16_t cad = det.getCadenceSpm();
    TEST_ASSERT_INT_WITHIN(20, 120, (int)cad);
}

static void test_reset_clears_count(void) {
    SoftwareStepDetector det;
    uint32_t ts = 0;
    feedPulses(det, ts, 100, 500, 10);
    uint16_t total = 0;
    det.read(total);
    TEST_ASSERT_EQUAL_UINT16(10, total);
    det.reset();
    det.read(total);
    TEST_ASSERT_EQUAL_UINT16(0, total);
}

// --- explicit runner (auto-runner not generated in this env) ----------------
int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_stationary_yields_zero);
    RUN_TEST(test_detects_known_step_count);
    RUN_TEST(test_reset_clears_count);
    return UNITY_END();
}
