#include <unity.h>
#include "step_codec.h"

// The IMU exposes the step count as a little-endian pair of registers
// (low byte first). Verify the assembly matches the documented order.

void test_combine_low_and_high(void) {
    TEST_ASSERT_EQUAL_UINT16(0x1234, combineStepBytes(0x34, 0x12));
}

void test_combine_zero(void) {
    TEST_ASSERT_EQUAL_UINT16(0x0000, combineStepBytes(0x00, 0x00));
}

void test_combine_max(void) {
    TEST_ASSERT_EQUAL_UINT16(0xFFFF, combineStepBytes(0xFF, 0xFF));
}

void test_combine_low_only_does_not_leak_into_high(void) {
    TEST_ASSERT_EQUAL_UINT16(0x00FF, combineStepBytes(0xFF, 0x00));
}

void test_combine_high_only(void) {
    TEST_ASSERT_EQUAL_UINT16(0xFF00, combineStepBytes(0x00, 0xFF));
}
