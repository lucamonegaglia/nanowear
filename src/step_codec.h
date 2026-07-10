#ifndef NANOWEAR_STEP_CODEC_H
#define NANOWEAR_STEP_CODEC_H

#include <stdint.h>

// Assemble a 16-bit step count from the LSM6DSOX's little-endian register pair:
// STEP_COUNTER_L holds the low byte, STEP_COUNTER_H holds the high byte.
//
// Isolated as a pure function so the byte-order maths is unit-testable without
// touching I2C. Mirrors the original expression `(highByte << 8) | lowByte`.
inline uint16_t combineStepBytes(uint8_t low, uint8_t high) {
    return static_cast<uint16_t>((static_cast<uint16_t>(high) << 8) | low);
}

#endif // NANOWEAR_STEP_CODEC_H
