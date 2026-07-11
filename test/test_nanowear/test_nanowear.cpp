#include <unity.h>
#include "pedometer.h"
#include "step_source.h"   // HardwareStepSource (swappable step seam)
#include "imu.h"
#include "step_codec.h"
#include "elapsed_timer.h"
#include "state_machine.h"
#include "test_ble.h"   // BLE-link test prototypes (defined in test/test_ble.cpp)
#include "test_gait.h"  // running-dynamics test prototypes

// ---------------------------------------------------------------------------
// NanoWear host test suite.
//
// This file lives in a `test_*`-prefixed directory (test/test_nanowear/) so
// PlatformIO discovers it as an explicit suite. That keeps CI robust: adding a
// second `test_*/` suite is additive, whereas a flat test/*.cpp layout relies
// on PlatformIO's `* fallback` suite, which is silently dropped the moment any
// `test_*` directory appears.
//
// NOTE: this environment's native + Unity setup does not auto-generate the
// test runner `main()`, so we provide one explicitly. Keep exactly ONE main()
// per suite — if you add another .cpp to this directory, do NOT define main()
// there. Add new tests below using the void test_*(void) convention;
// setUp()/tearDown() run around each.
//
// Coverage:
//   * step_codec    — little-endian step-byte assembly
//   * elapsed_timer — non-blocking interval boundary
//   * pedometer     — total/delta accumulation (MockIMU), I2C-failure handling,
//                     + simulated e2e loop
//   * state_machine — BOOT→LOGGING→SYNC / LOW_BATTERY transitions
// ---------------------------------------------------------------------------

static MockIMU mock;
static HardwareStepSource stepSrc(mock);  // swappable seam: wraps the IMU
static Pedometer pedo(stepSrc);
static StateMachine sm(2000);

void setUp(void) {
    mock = MockIMU();        // reset the sensor double
    pedo.reset();            // clear step accumulator
    sm = StateMachine(2000); // back to BOOT, 2s poll
}

void tearDown(void) {}

// --- step_codec -------------------------------------------------------------
void test_combine_low_and_high(void) {
    TEST_ASSERT_EQUAL_UINT16(0x1234, combineStepBytes(0x34, 0x12));
}
void test_combine_zero(void) {
    TEST_ASSERT_EQUAL_UINT16(0x0000, combineStepBytes(0x00, 0x00));
}
void test_combine_max(void) {
    TEST_ASSERT_EQUAL_UINT16(0xFFFF, combineStepBytes(0xFF, 0xFF));
}
void test_combine_low_only(void) {
    TEST_ASSERT_EQUAL_UINT16(0x00FF, combineStepBytes(0xFF, 0x00));
}
void test_combine_high_only(void) {
    TEST_ASSERT_EQUAL_UINT16(0xFF00, combineStepBytes(0x00, 0xFF));
}

// --- elapsed_timer ----------------------------------------------------------
void test_timer_not_elapsed_before_interval(void) {
    ElapsedTimer t(2000); t.reset(1000);
    TEST_ASSERT_FALSE(t.hasElapsed(1000));
    TEST_ASSERT_FALSE(t.hasElapsed(2999));
}
void test_timer_elapsed_exactly_at_interval(void) {
    ElapsedTimer t(2000); t.reset(1000);
    TEST_ASSERT_TRUE(t.hasElapsed(3000));
}
void test_timer_elapsed_after_interval(void) {
    ElapsedTimer t(2000); t.reset(1000);
    TEST_ASSERT_TRUE(t.hasElapsed(3001));
}
void test_timer_reset_restarts(void) {
    ElapsedTimer t(2000); t.reset(0);
    TEST_ASSERT_TRUE(t.hasElapsed(2000));
    t.reset(2000);
    TEST_ASSERT_FALSE(t.hasElapsed(2000));
    TEST_ASSERT_TRUE(t.hasElapsed(4000));
}
void test_timer_reports_duration(void) {
    ElapsedTimer t(2000); t.reset(500);
    TEST_ASSERT_EQUAL_UINT32(2500, t.elapsed(3000));
}

// --- pedometer --------------------------------------------------------------
void test_pedometer_initial_total_zero(void) {
    TEST_ASSERT_EQUAL_UINT16(0, pedo.getTotal());
}
void test_pedometer_first_update(void) {
    mock.stepCount = 100;
    TEST_ASSERT_EQUAL_UINT16(100, pedo.update());
    TEST_ASSERT_EQUAL_UINT16(100, pedo.getTotal());
}
void test_pedometer_accumulates(void) {
    mock.stepCount = 50; pedo.update();
    mock.stepCount = 120;
    TEST_ASSERT_EQUAL_UINT16(70, pedo.update());
    TEST_ASSERT_EQUAL_UINT16(120, pedo.getTotal());
}
void test_pedometer_no_change_zero_delta(void) {
    mock.stepCount = 80; pedo.update();
    TEST_ASSERT_EQUAL_UINT16(0, pedo.update());
    TEST_ASSERT_EQUAL_UINT16(80, pedo.getTotal());
}
void test_pedometer_reset(void) {
    mock.stepCount = 500; pedo.update();
    pedo.reset();
    TEST_ASSERT_EQUAL_UINT16(0, pedo.getTotal());
}
void test_pedometer_delta_clamped_backwards(void) {
    mock.stepCount = 200; pedo.update();
    mock.stepCount = 150;
    TEST_ASSERT_EQUAL_UINT16(0, pedo.update());
    TEST_ASSERT_EQUAL_UINT16(150, pedo.getTotal());
}
// A failed I2C read must NOT destroy already-accumulated steps: the running
// total is preserved and update() returns 0, then recovers on the next good read.
void test_pedometer_no_loss_on_read_failure(void) {
    mock.stepCount = 500; pedo.update();
    TEST_ASSERT_EQUAL_UINT16(500, pedo.getTotal());

    mock.readStepCountResult = false;   // simulate a bus error
    mock.stepCount = 0;                 // bogus value must be ignored
    TEST_ASSERT_EQUAL_UINT16(0, pedo.update());
    TEST_ASSERT_FALSE(pedo.readOk());
    TEST_ASSERT_EQUAL_UINT16(500, pedo.getTotal());

    mock.readStepCountResult = true;
    mock.stepCount = 520;
    TEST_ASSERT_EQUAL_UINT16(20, pedo.update());
    TEST_ASSERT_TRUE(pedo.readOk());
    TEST_ASSERT_EQUAL_UINT16(520, pedo.getTotal());
}
// Simulated end-to-end: what loop() does every 2 seconds.
void test_pedometer_e2e_loop_simulation(void) {
    const uint16_t readings[] = {0, 25, 25, 60, 130, 130, 131};
    const uint16_t totals[]   = {0, 25, 25, 60, 130, 130, 131};
    const uint16_t deltas[]   = {0, 25, 0, 35, 70, 0, 1};
    const int n = sizeof(readings) / sizeof(readings[0]);
    for (int i = 0; i < n; i++) {
        mock.stepCount = readings[i];
        uint16_t delta = pedo.update();
        TEST_ASSERT_EQUAL_UINT16(totals[i], pedo.getTotal());
        TEST_ASSERT_EQUAL_UINT16(deltas[i], delta);
    }
    TEST_ASSERT_EQUAL_UINT16(131, pedo.getTotal());
}

// --- state_machine ----------------------------------------------------------
void test_sm_starts_in_boot(void) {
    TEST_ASSERT_EQUAL_UINT8((uint8_t)TrackerState::BOOT, (uint8_t)sm.state());
    TEST_ASSERT_FALSE(sm.shouldPoll(0));
    TEST_ASSERT_FALSE(sm.shouldPoll(100000));
}
void test_sm_boot_enters_logging(void) {
    sm.startLogging(1000);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)TrackerState::LOGGING, (uint8_t)sm.state());
    TEST_ASSERT_FALSE(sm.shouldPoll(1000));
    TEST_ASSERT_FALSE(sm.shouldPoll(2999));
    TEST_ASSERT_TRUE(sm.shouldPoll(3000));
}
void test_sm_poll_rearm(void) {
    sm.startLogging(0);
    TEST_ASSERT_TRUE(sm.shouldPoll(2000));
    sm.markPolled(2000);
    TEST_ASSERT_FALSE(sm.shouldPoll(2000));
    TEST_ASSERT_TRUE(sm.shouldPoll(4000));
}
void test_sm_sync_cycle(void) {
    sm.startLogging(0);
    sm.requestSync();
    TEST_ASSERT_EQUAL_UINT8((uint8_t)TrackerState::SYNC, (uint8_t)sm.state());
    TEST_ASSERT_FALSE(sm.shouldPoll(99999));
    sm.syncComplete(5000);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)TrackerState::LOGGING, (uint8_t)sm.state());
    TEST_ASSERT_TRUE(sm.shouldPoll(7000));
}
void test_sm_low_battery_and_recover(void) {
    sm.startLogging(0);
    sm.enterLowBattery();
    TEST_ASSERT_EQUAL_UINT8((uint8_t)TrackerState::LOW_BATTERY, (uint8_t)sm.state());
    TEST_ASSERT_FALSE(sm.shouldPoll(99999));
    sm.recover(10);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)TrackerState::LOGGING, (uint8_t)sm.state());
    TEST_ASSERT_TRUE(sm.shouldPoll(2010));
}

// --- explicit runner (auto-runner not generated in this env) ----------------
int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_combine_low_and_high);
    RUN_TEST(test_combine_zero);
    RUN_TEST(test_combine_max);
    RUN_TEST(test_combine_low_only);
    RUN_TEST(test_combine_high_only);
    RUN_TEST(test_timer_not_elapsed_before_interval);
    RUN_TEST(test_timer_elapsed_exactly_at_interval);
    RUN_TEST(test_timer_elapsed_after_interval);
    RUN_TEST(test_timer_reset_restarts);
    RUN_TEST(test_timer_reports_duration);
    RUN_TEST(test_pedometer_initial_total_zero);
    RUN_TEST(test_pedometer_first_update);
    RUN_TEST(test_pedometer_accumulates);
    RUN_TEST(test_pedometer_no_change_zero_delta);
    RUN_TEST(test_pedometer_reset);
    RUN_TEST(test_pedometer_delta_clamped_backwards);
    RUN_TEST(test_pedometer_no_loss_on_read_failure);
    RUN_TEST(test_pedometer_e2e_loop_simulation);
    RUN_TEST(test_sm_starts_in_boot);
    RUN_TEST(test_sm_boot_enters_logging);
    RUN_TEST(test_sm_poll_rearm);
    RUN_TEST(test_sm_sync_cycle);
    RUN_TEST(test_sm_low_battery_and_recover);

    // --- BLE link (rsc_codec + MockBlePeripheral) ---------------------------
    RUN_TEST(test_rsc_flags_minimal);
    RUN_TEST(test_rsc_flags_combo);
    RUN_TEST(test_cadence_to_rsc_units);
    RUN_TEST(test_encode_rsc_measurement);
    RUN_TEST(test_encode_step_count_le);
    RUN_TEST(test_encode_step_count_zero);
    RUN_TEST(test_mock_begin_records_name);
    RUN_TEST(test_mock_begin_can_fail);
    RUN_TEST(test_mock_connection_reflects_flag);
    RUN_TEST(test_mock_notify_records_last_value);
    RUN_TEST(test_mock_reset_callback_invoked);
    RUN_TEST(test_mock_notify_gait_records_snapshot);

    // --- running dynamics (FIFO decode + gait detector + codec + seam) ---
    RUN_TEST(test_decode_fifo_roundtrip);
    RUN_TEST(test_gait_detector_recovers_contact_and_cadence);
    RUN_TEST(test_running_dynamics_codec_roundtrip);
    RUN_TEST(test_step_source_hardware_wraps_mock);
    RUN_TEST(test_step_source_software_counts_strides);
    RUN_TEST(test_pedometer_seam_hardware_step_source);
    return UNITY_END();
}
