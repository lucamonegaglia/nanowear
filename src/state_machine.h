#ifndef NANOWEAR_STATE_MACHINE_H
#define NANOWEAR_STATE_MACHINE_H

#include <stdint.h>
#include "elapsed_timer.h"

// ---------------------------------------------------------------------------
// TrackerState + StateMachine — non-blocking firmware orchestration (roadmap S1)
// ---------------------------------------------------------------------------
// Formalises the flat poll loop into a small state machine:
//
//        BOOT ──onBootComplete──▶ LOGGING ──requestSync──▶ SYNC
//         │                          ▲                        │
//         │ startLogging             │ syncComplete          │
//         ▼                          └────────────────────────┘
//        IDLE
//
//        (any active state) ──enterLowBattery──▶ LOW_BATTERY ──recover──▶ LOGGING
//
// It is driven entirely by injected time (`now`) plus explicit events, so the
// transition logic is fully unit-testable on the host with no board. The board
// firmware calls shouldPoll()/markPolled() from loop(); deep-sleep (S3) and BLE
// sync (S2/S5) will later hook into these same states without restructuring.
// ---------------------------------------------------------------------------
enum class TrackerState : uint8_t {
    BOOT,        // power-up / sensor init; never polls
    IDLE,        // armed but not accumulating (pre-activity standby)
    LOGGING,     // accumulating steps; polls the pedometer on an interval
    SYNC,        // (future) transferring the backlog to a phone over BLE
    LOW_BATTERY  // (future) critical battery; halts logging, may signal
};

class StateMachine {
public:
    explicit StateMachine(unsigned long pollIntervalMs)
        : pollTimer(pollIntervalMs) {}

    TrackerState state() const { return st; }

    // BOOT -> LOGGING once sensor init succeeds. Arms the poll timer.
    void onBootComplete(unsigned long now) {
        if (st == TrackerState::BOOT) {
            st = TrackerState::LOGGING;
            pollTimer.reset(now);
        }
    }

    // BOOT/IDLE -> LOGGING (e.g. an activity starts). Arms the poll timer.
    void startLogging(unsigned long now) {
        if (st == TrackerState::BOOT || st == TrackerState::IDLE) {
            st = TrackerState::LOGGING;
            pollTimer.reset(now);
        }
    }

    // True only while LOGGING and the poll interval has elapsed.
    bool shouldPoll(unsigned long now) const {
        return st == TrackerState::LOGGING && pollTimer.hasElapsed(now);
    }

    // Call after servicing a poll to re-arm the interval.
    void markPolled(unsigned long now) { pollTimer.reset(now); }

    // LOGGING -> SYNC (phone connected / user requests an upload).
    void requestSync() {
        if (st == TrackerState::LOGGING) st = TrackerState::SYNC;
    }

    // SYNC -> LOGGING once the transfer finishes; re-arms the poll timer.
    void syncComplete(unsigned long now) {
        if (st == TrackerState::SYNC) {
            st = TrackerState::LOGGING;
            pollTimer.reset(now);
        }
    }

    // Any active state -> LOW_BATTERY when the battery drops below threshold.
    void enterLowBattery() {
        if (st != TrackerState::LOW_BATTERY) st = TrackerState::LOW_BATTERY;
    }

    // LOW_BATTERY -> LOGGING once charged/recovered (future).
    void recover(unsigned long now) {
        if (st == TrackerState::LOW_BATTERY) {
            st = TrackerState::LOGGING;
            pollTimer.reset(now);
        }
    }

private:
    TrackerState st = TrackerState::BOOT;
    ElapsedTimer pollTimer;
};

#endif // NANOWEAR_STATE_MACHINE_H
