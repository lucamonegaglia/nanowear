#ifndef NANOWEAR_RSC_CODEC_H
#define NANOWEAR_RSC_CODEC_H

#include <stdint.h>

// ---------------------------------------------------------------------------
// rsc_codec — pure GATT payload encoders for the phone link
// ---------------------------------------------------------------------------
// The NanoWear peripheral advertises the Bluetooth SIG Running Speed and
// Cadence (RSC) service 0x1814 plus a custom NanoWear characteristic carrying
// the raw cumulative step count. These helpers build the exact little-endian
// byte layouts those characteristics carry, isolated as pure functions so the
// byte maths is unit-testable on the host with no radio (see test/test_ble.cpp).
//
// Reference: Bluetooth SIG RSC service / RSC Measurement (0x2A53) format.
// ---------------------------------------------------------------------------

// Build the RSC Measurement Flags byte.
//   bit0 = Instantaneous Stride Length Present
//   bit1 = Total Distance Present
//   bit2 = Walking/Running Status present
//   bit3 = Walking(0) / Running(1)
// We report cadence only (no stride length, no distance, no walk/run flag),
// so the minimal measurement = flags(1) + speed(2, =0 "unavailable") +
// cadence(1) = 4 octets, and flags = 0.
inline uint8_t rscFlags(bool stridePresent, bool distancePresent,
                        bool walkingRunningPresent, bool isRunning) {
    return static_cast<uint8_t>(
        (stridePresent ? 1 : 0) |
        ((distancePresent ? 1 : 0) << 1) |
        ((walkingRunningPresent ? 1 : 0) << 2) |
        ((isRunning ? 1 : 0) << 3));
}

// Convert a cadence in steps/minute to the RSC Measurement cadence field.
// Per the Bluetooth SIG RSC Measurement (0x2A53) the Instantaneous Cadence
// field is a uint8 in steps-per-minute (unit 1/minute, resolution 1) — there is
// NO sub-scaling, so the value is written as-is. e.g. 180 spm -> 180. (The
// fractional scaling belongs to the Instantaneous Speed field, 1/256 m/s, not
// to cadence.) Clamped to 255 so an out-of-range input can't wrap the uint8.
inline uint8_t cadenceToRscUnits(uint16_t stepsPerMinute) {
    return static_cast<uint8_t>(stepsPerMinute > 255 ? 255 : stepsPerMinute);
}

// Derive cadence (steps/minute) from the step delta over a time interval.
// This is the pure number-crunching half of notifySteps() pulled out so it is
// host-testable without a board or radio (see test/test_nanowear/test_ble.cpp).
//   delta = steps accrued since the previous sample
//   dtMs  = elapsed milliseconds for that delta
// Returns 0 when dtMs is 0 or sub-second (too short to give a stable rate) and
// uses uint64_t for the multiply so a large delta can't overflow the uint32.
inline uint16_t deriveCadenceSpm(uint32_t delta, uint32_t dtMs) {
    if (dtMs == 0) return 0;
    uint32_t dtSec = dtMs / 1000;     // integer seconds; sub-second -> 0
    if (dtSec == 0) return 0;
    return static_cast<uint16_t>(
        static_cast<uint64_t>(delta) * 60 / dtSec);
}

// Encode the minimal RSC Measurement payload: [flags, speedLo, speedHi, cadence].
// Speed is always present in the RSC Measurement (it is not flag-gated) and is
// written as 0 = 0.0 m/s (stationary), because the ankle unit measures steps,
// not speed. `cadenceUnits` is the pre-scaled value from cadenceToRscUnits().
// Writes exactly 4 bytes and returns the byte count. `out` must point to >= 4.
inline uint8_t encodeRscMeasurement(uint8_t* out, uint8_t flags,
                                    uint8_t cadenceUnits) {
    out[0] = flags;       // Flags
    out[1] = 0;           // Instantaneous Speed (uint16, 1/256 m/s) low  -> 0
    out[2] = 0;           // Instantaneous Speed high                     -> 0
    out[3] = cadenceUnits; // Instantaneous Cadence (steps/minute)
    return 4;
}

// Encode the custom NanoWear Steps characteristic value: a uint32 cumulative
// step count in little-endian. `out` must point to at least 4 bytes.
inline void encodeStepCount(uint8_t* out, uint32_t totalSteps) {
    out[0] = static_cast<uint8_t>(totalSteps & 0xFF);
    out[1] = static_cast<uint8_t>((totalSteps >> 8) & 0xFF);
    out[2] = static_cast<uint8_t>((totalSteps >> 16) & 0xFF);
    out[3] = static_cast<uint8_t>((totalSteps >> 24) & 0xFF);
}

// Sensor Location value we advertise: 0x03 = "Foot" (the device is worn on the
// ankle). Defined here so the board driver and tests agree on the constant.
static constexpr uint8_t RSC_SENSOR_LOCATION_FOOT = 0x03;

#endif // NANOWEAR_RSC_CODEC_H
