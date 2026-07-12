#ifndef NANOWEAR_DEBUG_CONSOLE_H
#define NANOWEAR_DEBUG_CONSOLE_H

#include <Arduino.h>
#include "imu.h"
#include "pedometer.h"
#include "state_machine.h"
#include "step_log.h"

// ---------------------------------------------------------------------------
// DebugConsole — USB-Serial command channel for development ("debug mode")
// ---------------------------------------------------------------------------
// Distinct from the eventual phone/Strava sync: this is a no-Bluetooth way to
// inspect and extract the in-RAM step log while the board is plugged into a
// laptop. It is board-only (depends on Serial) and excluded from the native
// test build, like hardware_imu.cpp.
//
// Non-blocking: call pollSerial() from loop(); it reads any pending byte and
// acts on single-character commands, ignoring everything else (newlines, etc.).
// The periodic [PEDOMETER] live print from main.cpp keeps running; dump lines
// are emitted as bare "<tMillis>,<totalSteps>" CSV bracketed by sentinels, so a
// host parser can grep them out of any other Serial output regardless of order.
//
// Commands:
//   r  reset to zero (HW counter + FW accumulator + log), resume LOGGING
//   d  enter DEBUG (pause polling)
//   g  resume LOGGING
//   l  dump the in-RAM log as CSV (between [LOG START] / [LOG END])
//   c  clear the log
//   s  status (state, total, buffer count, uptime)
//   ?/h help
// ---------------------------------------------------------------------------
class DebugConsole {
public:
    DebugConsole(StepLog<STEP_LOG_CAPACITY>& log, Pedometer& pedo,
                 StateMachine& sm, IMUSensor& imu)
        : log(log), pedo(pedo), sm(sm), imu(imu) {}

    // Read and dispatch any pending Serial input (one byte per call).
    void pollSerial();

private:
    void cmdDump();
    void cmdClear();
    void cmdReset();
    void cmdStatus();
    void cmdHelp();
    void cmdEnterDebug();
    void cmdResume();

    StepLog<STEP_LOG_CAPACITY>& log;
    Pedometer& pedo;
    StateMachine& sm;
    IMUSensor& imu;
};

#endif // NANOWEAR_DEBUG_CONSOLE_H
