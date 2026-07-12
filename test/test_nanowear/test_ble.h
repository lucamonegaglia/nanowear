#ifndef NANOWEAR_TEST_BLE_H
#define NANOWEAR_TEST_BLE_H

// Forward declarations of the BLE-link unit tests defined in test/test_ble.cpp.
// PlatformIO compiles each test/*.cpp as a separate translation unit but links
// them into one binary with a single runner (in test/test_nanowear.cpp). The
// runner's RUN_TEST(...) calls therefore need these prototypes in scope. Both
// translation units are C++, so the names mangle identically — no extern "C".
void test_rsc_flags_minimal(void);
void test_rsc_flags_combo(void);
void test_cadence_to_rsc_units(void);
void test_encode_rsc_measurement(void);
void test_encode_step_count_le(void);
void test_encode_step_count_zero(void);
void test_mock_begin_records_name(void);
void test_mock_begin_can_fail(void);
void test_mock_connection_reflects_flag(void);
void test_mock_notify_records_last_value(void);
void test_mock_reset_callback_invoked(void);
void test_derive_cadence_zero_interval(void);
void test_derive_cadence_basic(void);
void test_derive_cadence_zero_delta(void);

#endif // NANOWEAR_TEST_BLE_H
