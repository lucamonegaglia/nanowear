#include <unity.h>
#include "elapsed_timer.h"

// The firmware must never block on delay(). ElapsedTimer is the building block
// that lets loop() poll on an interval. These tests pin down its boundaries.

void test_not_elapsed_before_interval(void) {
    ElapsedTimer t(2000);
    t.reset(1000);
    TEST_ASSERT_FALSE(t.hasElapsed(1000));
    TEST_ASSERT_FALSE(t.hasElapsed(2999));
}

void test_elapsed_exactly_at_interval(void) {
    ElapsedTimer t(2000);
    t.reset(1000);
    TEST_ASSERT_TRUE(t.hasElapsed(3000));
}

void test_elapsed_after_interval(void) {
    ElapsedTimer t(2000);
    t.reset(1000);
    TEST_ASSERT_TRUE(t.hasElapsed(3001));
}

void test_reset_restarts_interval(void) {
    ElapsedTimer t(2000);
    t.reset(0);
    TEST_ASSERT_TRUE(t.hasElapsed(2000));
    t.reset(2000);
    TEST_ASSERT_FALSE(t.hasElapsed(2000));
    TEST_ASSERT_TRUE(t.hasElapsed(4000));
}

void test_elapsed_reports_correct_duration(void) {
    ElapsedTimer t(2000);
    t.reset(500);
    TEST_ASSERT_EQUAL_UINT32(2500, t.elapsed(3000));
}
