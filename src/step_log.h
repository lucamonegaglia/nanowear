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
// overwritten. One entry is recorded per successful pedometer poll:
//
//     { tMillis, total }   // millis() when polled, cumulative step count
//
// The buffer is pure logic — no Serial, no millis(), no I2C — so it is fully
// unit-testable on the host (native env) exactly like Pedometer. The .csv the
// user wants lands on the laptop, produced during a USB-Serial transfer
// (DebugConsole / scripts/dump_log.py), not stored on the board.
// ---------------------------------------------------------------------------
struct StepLogEntry {
    uint32_t tMillis;  // millis() at the poll
    uint16_t total;    // cumulative step count reported by the pedometer
};

// Number of entries retained. 1024 * 6 bytes ≈ 6 KB of the 270 KB RAM — about
// 34 minutes at the 2 s poll interval. Tune this single constant as needed.
constexpr size_t STEP_LOG_CAPACITY = 1024;

template <size_t CAPACITY>
class StepLog {
public:
    // Append one reading. When full, the oldest entry is overwritten (ring),
    // so count() never exceeds CAPACITY.
    void record(uint32_t tMillis, uint16_t total) {
        entries[head] = StepLogEntry{tMillis, total};
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
