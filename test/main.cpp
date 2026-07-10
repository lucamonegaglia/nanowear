#include <unity.h>

// ---------------------------------------------------------------------------
// Native (host) test entry point
// ---------------------------------------------------------------------------
// PlatformIO's `pio test` does not emit a Unity test runner (the `main` that
// calls RUN_TEST) for `platform = native` in this toolchain version, so we
// supply one. Each test_* function lives in a sibling test_*.cpp and is held
// in the MockStepSensor-free, deterministic logic under test.
//
// To add a test: implement `void test_xxx(void)` in a test_*.cpp, then declare
// it `extern` above and add a `RUN_TEST(test_xxx);` line inside main().
// ---------------------------------------------------------------------------

extern void test_initial_total_is_zero(void);
extern void test_update_returns_first_delta_and_total(void);
extern void test_update_accumulates_delta(void);
extern void test_no_new_steps_yields_zero_delta(void);
extern void test_reset_clears_total(void);
extern void test_delta_clamped_when_counter_goes_backwards(void);
extern void test_e2e_run_loop_simulation(void);

extern void test_not_elapsed_before_interval(void);
extern void test_elapsed_exactly_at_interval(void);
extern void test_elapsed_after_interval(void);
extern void test_reset_restarts_interval(void);
extern void test_elapsed_reports_correct_duration(void);

extern void test_combine_low_and_high(void);
extern void test_combine_zero(void);
extern void test_combine_max(void);
extern void test_combine_low_only_does_not_leak_into_high(void);
extern void test_combine_high_only(void);

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_initial_total_is_zero);
    RUN_TEST(test_update_returns_first_delta_and_total);
    RUN_TEST(test_update_accumulates_delta);
    RUN_TEST(test_no_new_steps_yields_zero_delta);
    RUN_TEST(test_reset_clears_total);
    RUN_TEST(test_delta_clamped_when_counter_goes_backwards);
    RUN_TEST(test_e2e_run_loop_simulation);
    RUN_TEST(test_not_elapsed_before_interval);
    RUN_TEST(test_elapsed_exactly_at_interval);
    RUN_TEST(test_elapsed_after_interval);
    RUN_TEST(test_reset_restarts_interval);
    RUN_TEST(test_elapsed_reports_correct_duration);
    RUN_TEST(test_combine_low_and_high);
    RUN_TEST(test_combine_zero);
    RUN_TEST(test_combine_max);
    RUN_TEST(test_combine_low_only_does_not_leak_into_high);
    RUN_TEST(test_combine_high_only);
    return UNITY_END();
}
