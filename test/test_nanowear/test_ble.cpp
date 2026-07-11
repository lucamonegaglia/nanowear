#include <unity.h>
#include "ble_peripheral.h"
#include "rsc_codec.h"

// ---------------------------------------------------------------------------
// NanoWear BLE link — host unit tests (no radio)
// ---------------------------------------------------------------------------
// Exercises the pure GATT encoders (rsc_codec.h) and the MockBlePeripheral
// test double. These run under `pio test -e native` with zero hardware, so the
// phone-link logic is verified without a NINA module or a phone. There is no
// explicit main() here — the single runner lives in test/test_nanowear.cpp
// and calls RUN_TEST on each of these.
// ---------------------------------------------------------------------------

// --- rsc_codec: flags -------------------------------------------------------
void test_rsc_flags_minimal(void) {
    // cadence-only (no stride/distance/walk-run) => flags == 0
    TEST_ASSERT_EQUAL_UINT8(0x00, rscFlags(false, false, false, false));
}
void test_rsc_flags_combo(void) {
    // stride + running => bit0 | bit3 == 0x09
    TEST_ASSERT_EQUAL_UINT8(0x09, rscFlags(true, false, false, true));
    // distance + walking/running present + walking => bit1 | bit2 == 0x06
    TEST_ASSERT_EQUAL_UINT8(0x06, rscFlags(false, true, true, false));
}

// --- rsc_codec: cadence scaling ---------------------------------------------
void test_cadence_to_rsc_units(void) {
    // spm/30 : 180 spm -> 6, 90 -> 3, 0 -> 0
    TEST_ASSERT_EQUAL_UINT8(6, cadenceToRscUnits(180));
    TEST_ASSERT_EQUAL_UINT8(3, cadenceToRscUnits(90));
    TEST_ASSERT_EQUAL_UINT8(0, cadenceToRscUnits(0));
    // truncation is acceptable for the coarse field
    TEST_ASSERT_EQUAL_UINT8(5, cadenceToRscUnits(160));
}

// --- rsc_codec: measurement payload -----------------------------------------
void test_encode_rsc_measurement(void) {
    uint8_t buf[4];
    uint8_t n = encodeRscMeasurement(buf, 0x00, 6);
    TEST_ASSERT_EQUAL_UINT8(4, n);          // flags + speed(2) + cadence(1)
    TEST_ASSERT_EQUAL_UINT8(0x00, buf[0]);  // flags
    TEST_ASSERT_EQUAL_UINT8(0x00, buf[1]);  // speed low  (0 = unavailable)
    TEST_ASSERT_EQUAL_UINT8(0x00, buf[2]);  // speed high
    TEST_ASSERT_EQUAL_UINT8(6,    buf[3]);  // cadence units (180 spm)
}

// --- rsc_codec: step count (custom characteristic) --------------------------
void test_encode_step_count_le(void) {
    uint8_t buf[4];
    encodeStepCount(buf, 0x12345678);
    TEST_ASSERT_EQUAL_UINT8(0x78, buf[0]);
    TEST_ASSERT_EQUAL_UINT8(0x56, buf[1]);
    TEST_ASSERT_EQUAL_UINT8(0x34, buf[2]);
    TEST_ASSERT_EQUAL_UINT8(0x12, buf[3]);
}
void test_encode_step_count_zero(void) {
    uint8_t buf[4];
    encodeStepCount(buf, 0);
    TEST_ASSERT_EQUAL_UINT8(0x00, buf[0]);
    TEST_ASSERT_EQUAL_UINT8(0x00, buf[1]);
    TEST_ASSERT_EQUAL_UINT8(0x00, buf[2]);
    TEST_ASSERT_EQUAL_UINT8(0x00, buf[3]);
}

// --- MockBlePeripheral: lifecycle -------------------------------------------
static int g_resetSideEffect = 0;
static void onResetSideEffect(void) { g_resetSideEffect++; }

void test_mock_begin_records_name(void) {
    MockBlePeripheral mock;
    TEST_ASSERT_FALSE(mock.beginCalled);
    bool ok = mock.begin("NanoWear");
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_TRUE(mock.beginCalled);
    TEST_ASSERT_EQUAL_STRING("NanoWear", mock.beginName);
}

void test_mock_begin_can_fail(void) {
    MockBlePeripheral mock;
    mock.beginResult = false;
    TEST_ASSERT_FALSE(mock.begin("X"));
}

void test_mock_connection_reflects_flag(void) {
    MockBlePeripheral mock;
    TEST_ASSERT_FALSE(mock.isConnected());
    mock.connected = true;
    TEST_ASSERT_TRUE(mock.isConnected());
}

void test_mock_notify_records_last_value(void) {
    MockBlePeripheral mock;
    mock.notifySteps(100);
    TEST_ASSERT_EQUAL_UINT32(100, mock.lastNotifiedSteps);
    TEST_ASSERT_EQUAL_INT(1, mock.notifyCallCount);
    mock.notifySteps(250);
    TEST_ASSERT_EQUAL_UINT32(250, mock.lastNotifiedSteps);
    TEST_ASSERT_EQUAL_INT(2, mock.notifyCallCount);
}

void test_mock_reset_callback_invoked(void) {
    MockBlePeripheral mock;
    g_resetSideEffect = 0;
    mock.onStepReset(&onResetSideEffect);
    TEST_ASSERT_NOT_NULL(mock.resetCb);
    // No central has asked yet:
    TEST_ASSERT_EQUAL_INT(0, mock.resetRequestsHandled);
    // Simulate the phone writing the reset command:
    mock.simulateResetRequest();
    TEST_ASSERT_EQUAL_INT(1, g_resetSideEffect);
    TEST_ASSERT_EQUAL_INT(1, mock.resetRequestsHandled);
}

// --- rsc_codec: cadence derivation (pure helper behind notifySteps) ----------
void test_derive_cadence_zero_interval(void) {
    // No time elapsed (or sub-second) -> undefined rate -> 0, never divide-by-0.
    TEST_ASSERT_EQUAL_UINT16(0, deriveCadenceSpm(0, 0));
    TEST_ASSERT_EQUAL_UINT16(0, deriveCadenceSpm(10, 0));
    TEST_ASSERT_EQUAL_UINT16(0, deriveCadenceSpm(10, 500));
}
void test_derive_cadence_basic(void) {
    // 6 steps in 2 s -> 3 steps/s -> 180 spm; 3 steps in 2 s -> 90 spm;
    // 1 step in 1 s -> 60 spm.
    TEST_ASSERT_EQUAL_UINT16(180, deriveCadenceSpm(6, 2000));
    TEST_ASSERT_EQUAL_UINT16(90,  deriveCadenceSpm(3, 2000));
    TEST_ASSERT_EQUAL_UINT16(60,  deriveCadenceSpm(1, 1000));
}
void test_derive_cadence_zero_delta(void) {
    // Time passing with no new steps -> 0 spm (not a divide error).
    TEST_ASSERT_EQUAL_UINT16(0, deriveCadenceSpm(0, 2000));
}

// --- MockBlePeripheral: dynamics snapshot --------------------------
void test_mock_notify_gait_records_snapshot(void) {
    MockBlePeripheral mock;
    GaitMetrics m;
    m.valid = true;
    m.contactTimeMs = 250.5f;
    m.cadenceSpm = 180.f;
    m.strike = StrikePattern::REARFOOT;
    mock.notifyGait(m);
    TEST_ASSERT_EQUAL_INT(1, mock.gaitNotifyCount);
    TEST_ASSERT_TRUE(mock.lastGait.valid);
    TEST_ASSERT_FLOAT_WITHIN(250.5f, mock.lastGait.contactTimeMs, 0.001f);
    TEST_ASSERT_FLOAT_WITHIN(180.0f, mock.lastGait.cadenceSpm, 0.001f);
    TEST_ASSERT_EQUAL_INT((int)StrikePattern::REARFOOT, (int)mock.lastGait.strike);
}

// ---------------------------------------------------------------------------
// NOTE: the RUN_TEST(...) registrations for the tests above live in
// test/test_nanowear.cpp's single main() so there is exactly one runner.
// ---------------------------------------------------------------------------
