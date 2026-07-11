#ifndef NANOWEAR_RUNNING_DYNAMICS_CODEC_H
#define NANOWEAR_RUNNING_DYNAMICS_CODEC_H

#include <stdint.h>
#include "gait_metrics.h"

// ---------------------------------------------------------------------------
// running_dynamics_codec — pure GATT payload encoders for the gait metrics
// ---------------------------------------------------------------------------
// The SIG RSC service (0x1814) the device already advertises carries only
// speed/cadence/stride — it CANNOT transport running dynamics. So they go on a
// custom NanoWear characteristic (see ble_peripheral_arduino.cpp). These
// helpers build the exact little-endian byte layout that characteristic carries,
// isolated as pure functions so the packing is unit-testable on the host with
// no radio (test/test_gait.cpp round-trips encode<->decode).
//
// Payload (10 octets, little-endian):
//   [0]      flags : bit0 valid, bits1-2 strike (0..3)
//   [1..2]   contact time      uint16, 0.1 ms  (time of contact / stance)
//   [3..4]   air time          uint16, 0.1 ms  (flight-time proxy)
//   [5..6]   cadence          uint16, 0.1 spm (steps/min * 10)
//   [7]      vertical osc.     uint8,  mm
//   [8]      foot-recovery     uint8,  deg/s / 2   (swing peak |pitch rate|)
//   [9]      braking index      uint8,  g * 100       (overstride proxy)
// Fields are scaled so a uint8/uint16 holds the realistic range; the decoder
// reverses the scaling so callers see physical units again.
// ---------------------------------------------------------------------------

static constexpr uint8_t GAIT_VALID_BIT   = 0x01;
static constexpr uint8_t GAIT_STRIKE_MASK = 0x06;   // bits 1-2
static constexpr uint8_t GAIT_STRIKE_SHIFT = 1;

// Pack `m` into `out` (must point to >= 10 bytes). Returns the byte count (10).
inline uint8_t encodeGaitMetrics(uint8_t* out, const GaitMetrics& m) {
    const uint8_t flags = static_cast<uint8_t>(
        (m.valid ? GAIT_VALID_BIT : 0) |
        ((static_cast<uint8_t>(m.strike) << GAIT_STRIKE_SHIFT) & GAIT_STRIKE_MASK));
    out[0] = flags;

    // Clamp helpers keep the scaled uint fields in range (defensive; realistic
    // running values sit well inside these ceilings).
    auto clampU16 = [](float v, float scale, uint16_t max) -> uint16_t {
        long x = static_cast<long>(v * scale + 0.5f);
        if (x < 0) x = 0;
        if (x > static_cast<long>(max)) x = static_cast<long>(max);
        return static_cast<uint16_t>(x);
    };
    auto clampU8 = [](float v, float scale, uint8_t max) -> uint8_t {
        long x = static_cast<long>(v * scale + 0.5f);
        if (x < 0) x = 0;
        if (x > static_cast<long>(max)) x = static_cast<long>(max);
        return static_cast<uint8_t>(x);
    };

    const uint16_t contact = clampU16(m.contactTimeMs, 10.f, 65535);   // 0.1 ms
    const uint16_t air     = clampU16(m.airTimeMs,     10.f, 65535);
    const uint16_t cad     = clampU16(m.cadenceSpm,   10.f, 65535);   // 0.1 spm
    out[1] = static_cast<uint8_t>(contact & 0xFF);
    out[2] = static_cast<uint8_t>((contact >> 8) & 0xFF);
    out[3] = static_cast<uint8_t>(air & 0xFF);
    out[4] = static_cast<uint8_t>((air >> 8) & 0xFF);
    out[5] = static_cast<uint8_t>(cad & 0xFF);
    out[6] = static_cast<uint8_t>((cad >> 8) & 0xFF);

    out[7] = clampU8(m.verticalOscillationMm, 1.f, 255);
    out[8] = clampU8(m.footRecoveryProxy,   1.f / 2.f, 255);  // deg/s / 2
    out[9] = clampU8(m.brakingIndex,          100.f,     255);  // g * 100
    return 10;
}

// Decode a 10-octet payload back into `m` (reverse of encodeGaitMetrics).
// `n` must be >= 10; fields outside the payload are left untouched.
inline void decodeGaitMetrics(const uint8_t* in, GaitMetrics& m) {
    const uint8_t flags = in[0];
    m.valid = (flags & GAIT_VALID_BIT) != 0;
    m.strike = static_cast<StrikePattern>(
        (flags & GAIT_STRIKE_MASK) >> GAIT_STRIKE_SHIFT);

    const uint16_t contact = static_cast<uint16_t>(in[1] | (in[2] << 8));
    const uint16_t air     = static_cast<uint16_t>(in[3] | (in[4] << 8));
    const uint16_t cad     = static_cast<uint16_t>(in[5] | (in[6] << 8));
    m.contactTimeMs = contact / 10.f;
    m.airTimeMs     = air / 10.f;
    m.cadenceSpm    = cad / 10.f;
    m.verticalOscillationMm = in[7];
    m.footRecoveryProxy    = in[8] * 2.f;       // deg/s
    m.brakingIndex        = in[9] / 100.f;     // g
}

#endif // NANOWEAR_RUNNING_DYNAMICS_CODEC_H
