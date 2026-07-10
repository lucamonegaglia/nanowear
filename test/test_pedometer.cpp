#include <unity.h>
#include "pedometer.h"
#include "imu.h"

// ---------------------------------------------------------------------------
// Pedometer unit + simulated end-to-end tests
// ---------------------------------------------------------------------------
// All tests drive the Pedometer through the MockStepSensor, which means no I2C and
// fully deterministic behaviour. The final test simulates the real firmware
// loop: poll on a 2s cadence, feed increasing hardware step counts, and verify
// the reported totals/deltas the firmware would print.
// ---------------------------------------------------------------------------

static MockStepSensor mock;
static Pedometer pedo(mock);

void setUp(void) {
    mock = MockStepSensor(); // reset the double between tests
    pedo.reset();
}

void test_initial_total_is_zero(void) {
    TEST_ASSERT_EQUAL_UINT16(0, pedo.getTotal());
}

void test_update_returns_first_delta_and_total(void) {
    mock.stepCount = 100;
    TEST_ASSERT_EQUAL_UINT16(100, pedo.update());
    TEST_ASSERT_EQUAL_UINT16(100, pedo.getTotal());
}

void test_update_accumulates_delta(void) {
    mock.stepCount = 50;
    pedo.update();                       // total 50, delta 50
    mock.stepCount = 120;
    TEST_ASSERT_EQUAL_UINT16(70, pedo.update()); // delta 70
    TEST_ASSERT_EQUAL_UINT16(120, pedo.getTotal());
}

void test_no_new_steps_yields_zero_delta(void) {
    mock.stepCount = 80;
    pedo.update();
    TEST_ASSERT_EQUAL_UINT16(0, pedo.update());
    TEST_ASSERT_EQUAL_UINT16(80, pedo.getTotal());
}

void test_reset_clears_total(void) {
    mock.stepCount = 500;
    pedo.update();
    pedo.reset();
    TEST_ASSERT_EQUAL_UINT16(0, pedo.getTotal());
}

void test_delta_clamped_when_counter_goes_backwards(void) {
    // Hardware counter must not produce negative deltas (e.g. missed reset).
    mock.stepCount = 200;
    pedo.update();
    mock.stepCount = 150; // spurious lower reading
    TEST_ASSERT_EQUAL_UINT16(0, pedo.update());
    TEST_ASSERT_EQUAL_UINT16(150, pedo.getTotal());
}

// Simulated end-to-end: what the firmware's loop() does every 2 seconds.
void test_e2e_run_loop_simulation(void) {
    // Sequence of hardware readings at each poll tick.
    const uint16_t readings[] = {0, 25, 25, 60, 130, 130, 131};
    const uint16_t expectedTotals[] = {0, 25, 25, 60, 130, 130, 131};
    const uint16_t expectedDeltas[] = {0, 25, 0, 35, 70, 0, 1};

    const int n = sizeof(readings) / sizeof(readings[0]);
    for (int i = 0; i < n; i++) {
        mock.stepCount = readings[i];
        uint16_t delta = pedo.update();
        TEST_ASSERT_EQUAL_UINT16(expectedTotals[i], pedo.getTotal());
        TEST_ASSERT_EQUAL_UINT16(expectedDeltas[i], delta);
    }

    // The loop would only log "new steps" when delta > 0: that happens on
    // reads 1, 3, 4 and 6 above — exactly where delta is non-zero.
    TEST_ASSERT_EQUAL_UINT16(131, pedo.getTotal());
}
