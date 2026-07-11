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
// Per the SIG spec the field unit is 0.5 steps/second, so the encoded value is
// spm / 30 (spm/60 steps-per-sec * 2 half-steps-per-sec). e.g. 180 spm -> 6.
inline uint8_t cadenceToRscUnits(uint16_t stepsPerMinute) {
    return static_cast<uint8_t>(stepsPerMinute / 30);
}

// Encode the minimal RSC Measurement payload: [flags, speedLo, speedHi, cadence].
// Speed is always present in the RSC Measurement and is written as 0 ("not
// available") because the ankle unit measures steps, not speed. `cadenceUnits`
// is the pre-scaled value from cadenceToRscUnits(). Writes exactly 4 bytes and
// returns the byte count. `out` must point to at least 4 bytes.
inline uint8_t encodeRscMeasurement(uint8_t* out, uint8_t flags,
                                    uint8_t cadenceUnits) {
    out[0] = flags;       // Flags
    out[1] = 0;           // Instantaneous Speed (uint16, m/s * 256) low  -> 0
    out[2] = 0;           // Instantaneous Speed high                     -> 0
    out[3] = cadenceUnits; // Instantaneous Cadence (0.5 steps/sec)
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
