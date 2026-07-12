#ifndef NANOWEAR_IMU_FIFO_H
#define NANOWEAR_IMU_FIFO_H

#include <stdint.h>
#include <string.h>   // memcpy

// ---------------------------------------------------------------------------
// IMU FIFO transport — raw-sample types + a host-testable byte parser
// ---------------------------------------------------------------------------
// The LSM6DSOX's embedded pedometer (MLC) only emits a step count; it cannot
// supply the raw 6-axis stream the gait detector needs. To get running dynamics
// we ALSO stream the sensor's FIFO (accel + gyro at high ODR) and decode it.
//
// This header defines the shared vocabulary for that stream and isolates the
// byte-level decoding as a PURE function (decodeFifo) so it is unit-testable
// on the host with no I2C. The board-only FifoSource implementation (in
// hardware_imu.cpp) performs the actual I2C burst reads and feeds decodeFifo.
//
// Units convention (keeps the detector math readable):
//   * accel axes are in g (1.0 == one gravity)
//   * gyro  axes are in degrees/second
//   * ts      is a free-running millisecond timestamp (ODR-derived; see reader)
// ---------------------------------------------------------------------------

// One decoded IMU sample (already scaled out of raw register units).
struct ImuSample {
    float ax = 0, ay = 0, az = 0;   // accelerometer, g
    float gx = 0, gy = 0, gz = 0;   // gyroscope, deg/s
    uint32_t ts = 0;                    // timestamp, ms
};

// Describes what is packed into each FIFO sample so the parser stays generic.
// For Tier A we enable accel + gyro with no embedded timestamp: that is a
// fixed 12-byte pattern (3 axes * 2 bytes * 2 sensors). Add fields here
// later (e.g. temperature, external sensor, FIFO timestamp) without touching
// the detector — decodeFifo grows to match.
struct FifoPattern {
    bool accel = true;
    bool gyro  = true;
    bool timestamp = false;   // LSM6DSOX embedded FIFO timestamp (3 bytes)

    // Bytes occupied by one sample in this pattern. Computed from the flags so
    // callers don't hard-code 12. accel/gyro contribute 6 bytes each.
    uint8_t bytesPerSample() const {
        uint8_t n = 0;
        if (accel)    n += 6;
        if (gyro)     n += 6;
        if (timestamp) n += 3;
        return n;
    }
};

// ---------------------------------------------------------------------------
// FifoSource — abstract burst reader (mirrors the IMUSensor seam)
// ---------------------------------------------------------------------------
// The gait detector depends ONLY on this interface, so it is testable with a
// MockFifoSource on the host. The production implementation lives in
// hardware_imu.cpp (board only), which does the I2C reads against the
// LSM6DSOX FIFO_DATA_OUT register.
//
// read() returns a burst of raw FIFO bytes (NOT decoded samples) so the byte
// stream — including the sensor's own structure — is what crosses the seam.
// The caller then runs decodeFifo(). This keeps the I2C transport and the
// parsing as two independently testable pieces.
// ---------------------------------------------------------------------------
class FifoSource {
public:
    virtual ~FifoSource() = default;

    // Read up to `cap` raw FIFO bytes into `out`. Returns the number of bytes
    // actually placed (0 if empty / transport error) and true/false via the
    // bool return to distinguish "empty" (false-positive-safe) from "error".
    // The board impl drains the FIFO until the watermark clears or `cap` is hit.
    virtual bool read(uint8_t* out, size_t cap, size_t& filled) = 0;
};

// ---------------------------------------------------------------------------
// MockFifoSource — test double for the host (native) unit tests
// ---------------------------------------------------------------------------
// Holds a fixed byte buffer the test preloads; read() replays it once (then
// returns 0) so tests can assert on decodeFifo output without any hardware.
// ---------------------------------------------------------------------------
class MockFifoSource : public FifoSource {
public:
    const uint8_t* data = nullptr;
    size_t len = 0;

    bool read(uint8_t* out, size_t cap, size_t& filled) override {
        if (!data || len == 0) { filled = 0; return false; }
        filled = (len < cap) ? len : cap;
        memcpy(out, data, filled);
        // Consume so a second read reports empty (like a drained FIFO).
        data = nullptr;
        len = 0;
        return true;
    }
};

// ---------------------------------------------------------------------------
// decodeFifo — pure byte-stream -> ImuSample[] decoder
// ---------------------------------------------------------------------------
// Walks `buf` (exactly `len` raw FIFO bytes) in `pattern.bytesPerSample()`
// strides, decodes each little-endian int16 axis, scales it to physical
// units via `aScale` (g per LSB) and `gScale` (deg/s per LSB), and writes
// `ImuSample`s into `out` (up to `maxOut`). `tsBase` is the (fractional-ms)
// timestamp of the FIRST sample; each subsequent sample is stamped
// `tsBase + i*dtMs` so the caller only needs the ODR (not the sensor's own
// timestamp) to reconstruct timing. `tsBase` is left pointing just past the
// last sample for chaining across bursts.
//
// dtMs and tsBase are FLOAT milliseconds on purpose: at 1.66 kHz the period is
// ~0.602 ms/sample, which a uint32 dt would truncate to 0 — collapsing every
// timestamp and starving the detector's plausibility gates. Accumulating time
// as float and rounding only at the per-sample stamp keeps stride intervals
// accurate to ~±1 ms even though each increment is sub-millisecond.
//
// Returns the number of samples decoded. If `len` is not a whole number of
// samples the trailing partial bytes are ignored (defensive; the FIFO reader
// guarantees whole samples, but a corrupted burst must not desync the parser).
// ---------------------------------------------------------------------------
inline size_t decodeFifo(const uint8_t* buf, size_t len,
                         const FifoPattern& pattern,
                         float aScale, float gScale,
                         float dtMs, float& tsBase,
                         ImuSample* out, size_t maxOut) {
    const uint8_t bps = pattern.bytesPerSample();
    if (bps == 0 || bps > len) return 0;

    size_t nSamples = len / bps;
    if (nSamples > maxOut) nSamples = maxOut;

    for (size_t i = 0; i < nSamples; i++) {
        const uint8_t* p = buf + i * bps;
        ImuSample& s = out[i];
        size_t off = 0;

        auto le16 = [&](const uint8_t* q) -> int16_t {
            return static_cast<int16_t>(static_cast<uint16_t>(q[0]) |
                                       (static_cast<uint16_t>(q[1]) << 8));
        };

        if (pattern.accel) {
            s.ax = le16(p + off) * aScale; off += 2;
            s.ay = le16(p + off) * aScale; off += 2;
            s.az = le16(p + off) * aScale; off += 2;
        }
        if (pattern.gyro) {
            s.gx = le16(p + off) * gScale; off += 2;
            s.gy = le16(p + off) * gScale; off += 2;
            s.gz = le16(p + off) * gScale; off += 2;
        }
        // (timestamp flag reserved for future patterns; not consumed yet)

        // Round the CUMULATIVE time (not the increment) so sub-ms periods
        // still advance the integer-ms stamp correctly.
        s.ts = static_cast<uint32_t>(tsBase + static_cast<float>(i) * dtMs + 0.5f);
    }

    tsBase += static_cast<float>(nSamples) * dtMs;
    return nSamples;
}

#endif // NANOWEAR_IMU_FIFO_H
