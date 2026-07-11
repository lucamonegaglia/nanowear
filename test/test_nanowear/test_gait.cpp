#include <unity.h>
#include <cmath>
#include "imu_fifo.h"            // FifoPattern, decodeFifo, MockFifoSource
#include "gait_detector.h"       // GaitDetector
#include "running_dynamics_codec.h" // encode/decode GaitMetrics
#include "step_source.h"         // HardwareStepSource / SoftwareStepSource
#include "pedometer.h"           // Pedometer (swappable seam)
#include "imu.h"                 // MockIMU

// ---------------------------------------------------------------------------
// Running-dynamics host tests (no board, no I2C)
// ---------------------------------------------------------------------------
// Covers the pure logic Tier A adds:
//   * decodeFifo      — byte-stream -> ImuSample round-trip
//   * GaitDetector    — on a synthetic running signal it recovers contact
//                        time / step time / cadence and a classified strike
//   * codec            — GaitMetrics pack/unpack round-trip
//   * StepSource seam  — HardwareStepSource wraps MockIMU; SoftwareStepSource
//                        counts strides; Pedometer still works through the seam
// ---------------------------------------------------------------------------

// --- decodeFifo: build raw bytes for two known samples, decode, compare ---
void test_decode_fifo_roundtrip(void) {
    // Two samples: (ax,ay,az,gx,gy,gz) in raw LSB units.
    const int16_t s0[6] = { 100, -200, 300, 10, -20, 30 };
    const int16_t s1[6] = { -50, 400, -10, 0, 15, -5 };

    uint8_t buf[24];
    auto put = [](uint8_t* p, int16_t v) {
        p[0] = static_cast<uint8_t>(v & 0xFF);
        p[1] = static_cast<uint8_t>((v >> 8) & 0xFF);
    };
    put(&buf[0],  s0[0]); put(&buf[2],  s0[1]); put(&buf[4],  s0[2]);
    put(&buf[6],  s0[3]); put(&buf[8],  s0[4]); put(&buf[10], s0[5]);
    put(&buf[12], s1[0]); put(&buf[14], s1[1]); put(&buf[16], s1[2]);
    put(&buf[18], s1[3]); put(&buf[20], s1[4]); put(&buf[22], s1[5]);

    // scale = 1 so raw LSB == physical unit (exact comparison).
    FifoPattern pat;   // default: accel + gyro, no timestamp
    ImuSample out[4];
    uint32_t tsBase = 0;
    size_t n = decodeFifo(buf, sizeof(buf), pat, 1.f, 1.f, 1.f, tsBase, out, 4);
    TEST_ASSERT_EQUAL_UINT32(2u, (uint32_t)n);

    TEST_ASSERT_EQUAL_INT16(s0[0], (int16_t)out[0].ax);
    TEST_ASSERT_EQUAL_INT16(s0[1], (int16_t)out[0].ay);
    TEST_ASSERT_EQUAL_INT16(s0[2], (int16_t)out[0].az);
    TEST_ASSERT_EQUAL_INT16(s0[3], (int16_t)out[0].gx);
    TEST_ASSERT_EQUAL_INT16(s0[4], (int16_t)out[0].gy);
    TEST_ASSERT_EQUAL_INT16(s0[5], (int16_t)out[0].gz);
    TEST_ASSERT_EQUAL_INT16(s1[0], (int16_t)out[1].ax);
    TEST_ASSERT_EQUAL_INT16(s1[2], (int16_t)out[1].az);
    TEST_ASSERT_EQUAL_INT16(s1[3], (int16_t)out[1].gx);
    // timestamps assigned from tsBase + i*dtMs
    TEST_ASSERT_EQUAL_UINT32(0, out[0].ts);
    TEST_ASSERT_EQUAL_UINT32(1, out[1].ts);
    TEST_ASSERT_EQUAL_UINT32(2, tsBase);   // advanced past both samples
}

// --- GaitDetector: synthetic running signal -> measured metrics ---------
// Calibration window (still), then a pitch-rate signal with a minimum every
// 250 ms (stance). The alternating minima become IC/TC, so contact = 250
// ms and stride = 500 ms => cadence ~120 spm.
void test_gait_detector_recovers_contact_and_cadence(void) {
    GaitDetector det(1000.f);   // fs = 1 kHz -> dt = 1 ms
    ImuSample s;

    // Calibration: 64 still samples (az ~ 1g, no rotation).
    for (int i = 0; i < 64; i++) {
        s.ax = 0; s.ay = 0; s.az = 1;
        s.gx = 0; s.gy = 0; s.gz = 0;
        s.ts = static_cast<uint32_t>(i);
        det.process(s);
    }
    TEST_ASSERT_TRUE(det.isCalibrated());

    // Running: pitch rate gx = -cos(2*pi*t/250) => minima every 250 ms.
    // Flat accel => vertical oscillation / braking stay ~0 (proxies only).
    const float kTwoPi = 6.2831853f;
    for (int i = 64; i <= 820; i++) {
        float t = static_cast<float>(i);
        s.ax = 0; s.ay = 0; s.az = 1;
        s.gx = -cosf(kTwoPi * t / 250.0f);   // pitch rate (pitch axis = x)
        s.gy = 0; s.gz = 0;
        s.ts = static_cast<uint32_t>(i);
        det.process(s);
    }

    TEST_ASSERT_TRUE(det.hasNewMetrics());
    const GaitMetrics& m = det.metrics();
    TEST_ASSERT_TRUE(m.valid);
    // Contact (IC->TC) ~ 250 ms; stride (IC->IC) ~ 500 ms; cadence ~ 120.
    TEST_ASSERT_FLOAT_WITHIN(250.0f, m.contactTimeMs, 15.0f);
    TEST_ASSERT_FLOAT_WITHIN(500.0f, m.stepTimeMs,    15.0f);
    TEST_ASSERT_FLOAT_WITHIN(120.0f, m.cadenceSpm,     4.0f);
    // Strike must be classified (not UNKNOWN); proxies must be finite + non-negative.
    TEST_ASSERT_TRUE(m.strike != StrikePattern::UNKNOWN);
    TEST_ASSERT_TRUE(m.verticalOscillationMm >= 0.0f);
    TEST_ASSERT_TRUE(m.brakingIndex >= 0.0f);
}

// --- codec: GaitMetrics pack/unpack round-trip ---------------------------
void test_running_dynamics_codec_roundtrip(void) {
    GaitMetrics m;
    m.valid = true;
    m.contactTimeMs = 250.5f;
    m.airTimeMs     = 200.3f;
    m.stepTimeMs     = 500.0f;
    m.cadenceSpm    = 123.4f;
    m.verticalOscillationMm = 87.0f;
    m.footRecoveryProxy    = 123.5f;   // -> /2 = 61.75 -> 62 -> *2 = 124
    m.brakingIndex        = 1.23f;     // -> *100 = 123
    m.strike = StrikePattern::REARFOOT;

    uint8_t buf[10];
    TEST_ASSERT_EQUAL_UINT8(10, encodeGaitMetrics(buf, m));

    GaitMetrics d;
    decodeGaitMetrics(buf, d);
    TEST_ASSERT_TRUE(d.valid);
    TEST_ASSERT_EQUAL_INT((int)StrikePattern::REARFOOT, (int)d.strike);
    TEST_ASSERT_FLOAT_WITHIN(250.5f, d.contactTimeMs, 0.05f);
    TEST_ASSERT_FLOAT_WITHIN(500.0f, d.stepTimeMs,    0.05f);
    TEST_ASSERT_FLOAT_WITHIN(123.4f, d.cadenceSpm,   0.05f);
    TEST_ASSERT_FLOAT_WITHIN(87.0f,  d.verticalOscillationMm, 0.5f);
    TEST_ASSERT_FLOAT_WITHIN(124.0f, d.footRecoveryProxy,   1.0f);
    TEST_ASSERT_FLOAT_WITHIN(1.23f,  d.brakingIndex,       0.01f);
}

// --- StepSource: HardwareStepSource forwards to MockIMU -----------------
void test_step_source_hardware_wraps_mock(void) {
    MockIMU mock;
    HardwareStepSource src(mock);
    mock.stepCount = 123;
    uint16_t v = 0;
    TEST_ASSERT_TRUE(src.read(v));
    TEST_ASSERT_EQUAL_UINT16(123, v);
    // transport failure is surfaced
    mock.readStepCountResult = false;
    TEST_ASSERT_FALSE(src.read(v));
}

// --- StepSource: SoftwareStepSource counts strides -----------------------
void test_step_source_software_counts_strides(void) {
    SoftwareStepSource src;
    uint16_t v = 0;
    TEST_ASSERT_TRUE(src.read(v));
    TEST_ASSERT_EQUAL_UINT16(0, v);
    src.onStride();
    src.onStride();
    src.onStride();
    TEST_ASSERT_TRUE(src.read(v));
    TEST_ASSERT_EQUAL_UINT16(3, v);
}

// --- Pedometer still works through the new StepSource seam --------------
void test_pedometer_seam_hardware_step_source(void) {
    MockIMU mock;
    HardwareStepSource src(mock);
    Pedometer pedo(src);      // depends on StepSource, not IMUSensor directly

    mock.stepCount = 50;
    TEST_ASSERT_EQUAL_UINT16(50, pedo.update());
    TEST_ASSERT_EQUAL_UINT16(50, pedo.getTotal());

    mock.stepCount = 120;
    TEST_ASSERT_EQUAL_UINT16(70, pedo.update());
    TEST_ASSERT_EQUAL_UINT16(120, pedo.getTotal());

    // transport failure: total preserved, update() returns 0
    mock.readStepCountResult = false;
    mock.stepCount = 0;
    TEST_ASSERT_EQUAL_UINT16(0, pedo.update());
    TEST_ASSERT_FALSE(pedo.readOk());
    TEST_ASSERT_EQUAL_UINT16(120, pedo.getTotal());
}
