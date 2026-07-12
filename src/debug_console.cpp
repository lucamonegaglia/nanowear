#include "debug_console.h"

// ---------------------------------------------------------------------------
// DebugConsole — see debug_console.h for the command protocol
// ---------------------------------------------------------------------------

void DebugConsole::pollSerial() {
    if (!Serial.available()) return;
    char c = Serial.read();
    switch (c) {
        case 'r': cmdReset();      break; // reset counters + log to zero
        case 'd': cmdEnterDebug(); break; // pause polling
        case 'g': cmdResume();     break; // resume logging
        case 'l': cmdDump();       break; // dump the in-RAM log as CSV
        case 'c': cmdClear();      break; // clear the log
        case 's': cmdStatus();     break; // show status
        case '?':
        case 'h': cmdHelp();       break; // help
        default: break;                        // ignore newlines / noise
    }
}

void DebugConsole::cmdDump() {
    Serial.println("[LOG START]");
    for (size_t i = 0; i < log.count(); i++) {
        StepLogEntry e = log.get(i);
        Serial.print(e.tMillis);
        Serial.print(',');
        Serial.println(e.total);
    }
    Serial.println("[LOG END]");
}

void DebugConsole::cmdClear() {
    log.clear();
    Serial.println("[OK] log cleared");
}

void DebugConsole::cmdReset() {
    // Only zero the firmware accumulator + log after the HARDWARE counter has
    // actually been reset. If the I2C write to PEDO_CMD_REG fails, leaving the
    // FW total at 0 while the HW counter keeps its old value would make the next
    // update() report a bogus delta equal to the whole hardware total (the exact
    // failure mode pedometer.h warns about).
    if (!imu.resetStepCount()) {
        Serial.println("[ERROR] step-counter hardware reset failed (I2C error)");
        return;
    }
    pedo.reset();         // zero the firmware accumulator
    log.clear();          // drop the old history
    // Resume LOGGING if we were paused in DEBUG; otherwise (already LOGGING) just
    // re-arm the poll timer so the next sample is a clean 2s later.
    if (sm.state() == TrackerState::DEBUG) sm.resumeLogging(millis());
    else sm.markPolled(millis());
    Serial.println("[OK] step counters and log reset to 0");
}

void DebugConsole::cmdStatus() {
    Serial.print("[STATUS] state=");
    Serial.print(static_cast<int>(sm.state()));
    Serial.print(" total=");
    Serial.print(pedo.getTotal());
    Serial.print(" logCount=");
    Serial.print(static_cast<unsigned>(log.count()));
    Serial.print(" uptime=");
    Serial.print(millis());
    Serial.println("ms");
}

void DebugConsole::cmdHelp() {
    Serial.println("[HELP]");
    Serial.println("  r  reset step counters + log to 0");
    Serial.println("  d  enter DEBUG (pause logging)");
    Serial.println("  g  resume logging");
    Serial.println("  l  dump in-RAM step log (CSV: <tMillis>,<totalSteps>)");
    Serial.println("  c  clear the log");
    Serial.println("  s  show status");
    Serial.println("  ?  show this help");
}

void DebugConsole::cmdEnterDebug() {
    sm.enterDebug();
    Serial.println("[OK] DEBUG: polling paused");
}

void DebugConsole::cmdResume() {
    sm.resumeLogging(millis());
    Serial.println("[OK] LOGGING resumed");
}
