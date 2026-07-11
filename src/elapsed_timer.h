#ifndef NANOWEAR_ELAPSED_TIMER_H
#define NANOWEAR_ELAPSED_TIMER_H

#include <stdint.h>

// ---------------------------------------------------------------------------
// ElapsedTimer — non-blocking millisecond timer
// ---------------------------------------------------------------------------
// Enforces the project's "no delay()" rule: instead of sleeping, the firmware
// polls hasElapsed() from loop() and only acts when the interval has passed.
// All time is injected (the board passes millis()), which keeps it testable.
// ---------------------------------------------------------------------------
class ElapsedTimer {
public:
    explicit ElapsedTimer(unsigned long intervalMs) : intervalMs(intervalMs) {}

    // (Re)start the timer from `now`.
    void reset(unsigned long now) { lastTick = now; }

    // Milliseconds elapsed since the last reset.
    unsigned long elapsed(unsigned long now) const { return now - lastTick; }

    // True once `intervalMs` or more has elapsed since the last reset.
    bool hasElapsed(unsigned long now) const {
        return (now - lastTick) >= intervalMs;
    }

private:
    unsigned long lastTick = 0;
    unsigned long intervalMs = 0;
};

#endif // NANOWEAR_ELAPSED_TIMER_H
