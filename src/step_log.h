#ifndef NANOWEAR_STEP_LOG_H
#define NANOWEAR_STEP_LOG_H

#include <stdint.h>

// ---------------------------------------------------------------------------
// StepLog — bounded in-RAM ring buffer of step readings ("memory management")
// ---------------------------------------------------------------------------
// The board has no filesystem wired into its toolchain (Arduino Mbed OS, no
// LittleFS), so "saving the logs" means keeping a FIXED-SIZE history in RAM.
// This ring buffer is the memory-management policy: capacity is fixed at
// compile time, so RAM usage never grows, and once full the oldest entry is
// overwritten. In DEBUG mode one entry is recorded per raw-motion sample (a
// fixed high rate, see RAW_SAMPLE_MS in main.cpp):
//
//     { tMillis, total, ax, ay, az, gx, gy, gz }
//        millis() at sample | cumulative step count | live accel (g) | live gyro (deg/s)
//
// Carrying the raw accel/gyro lets the gait be analysed from the motion signal
// itself, independent of the embedded pedometer's threshold / debounce logic.
//
// The buffer is pure logic — no Serial, no millis(), no I2C — so it is fully
// unit-testable on the host (native env) exactly like Pedometer. The .csv the
// user wants lands on the laptop, produced during a USB-Serial transfer
// (DebugConsole / scripts/dump_log.py), not stored on the board.
// ---------------------------------------------------------------------------
struct StepLogEntry {
    uint32_t tMillis;  // millis() at the sample
    uint16_t total;    // cumulative step count reported by the pedometer
    float ax, ay, az;  // live accelerometer (g) at the sample — the raw motion
                       // signal, independent of the pedometer's internal
                       // threshold / debounce logic
    float gx, gy, gz;  // live gyroscope (deg/s) at the sample
};

// Number of entries retained. 1024 * 30 bytes ≈ 30 KB of the 270 KB RAM — about
// a 41 s capture window at the 40 ms raw-sample interval (25 Hz). The buffer is
// a ring: once full the oldest sample is overwritten, so a dump after a walk
// shows only the most recent window. Tune this single constant as needed.
constexpr size_t STEP_LOG_CAPACITY = 1024;

template <size_t CAPACITY>
class StepLog {
public:
    // Append one reading. When full, the oldest entry is overwritten (ring),
    // so count() never exceeds CAPACITY. The 6 motion floats default to 0 so
    // callers that only track steps (e.g. unit tests, the BLE build) need not
    // supply them.
    void record(uint32_t tMillis, uint16_t total,
                float ax = 0, float ay = 0, float az = 0,
                float gx = 0, float gy = 0, float gz = 0) {
        entries[head] = StepLogEntry{tMillis, total, ax, ay, az, gx, gy, gz};
        head = (head + 1) % CAPACITY;
        if (count_ < CAPACITY) count_++;
    }

    // Number of valid entries (0 .. CAPACITY).
    size_t count() const { return count_; }

    // Entry i in insertion order (0 = oldest, count()-1 = newest).
    // Caller must ensure i < count().
    StepLogEntry get(size_t i) const {
        size_t idx = (count_ < CAPACITY) ? i : (head + i) % CAPACITY;
        return entries[idx];
    }

    void clear() { head = 0; count_ = 0; }

private:
    StepLogEntry entries[CAPACITY];
    size_t head = 0;   // next write position
    size_t count_ = 0; // valid entries (caps at CAPACITY)
};

#endif // NANOWEAR_STEP_LOG_H
