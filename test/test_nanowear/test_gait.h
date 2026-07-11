#ifndef NANOWEAR_TEST_GAIT_H
#define NANOWEAR_TEST_GAIT_H

// Forward declarations of the running-dynamics unit tests defined in
// test/test_gait.cpp. Linked into the single runner in test_nanowear.cpp.
void test_decode_fifo_roundtrip(void);
void test_gait_detector_recovers_contact_and_cadence(void);
void test_running_dynamics_codec_roundtrip(void);
void test_step_source_hardware_wraps_mock(void);
void test_step_source_software_counts_strides(void);
void test_pedometer_seam_hardware_step_source(void);

#endif // NANOWEAR_TEST_GAIT_H
