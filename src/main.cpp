#include <Arduino.h>
#include <WiFiNINA.h>            // onboard RGB LED (LEDR/LEDG/LEDB); active-low
#include "hardware_imu.h"
#include "pedometer.h"
#include "state_machine.h"
#include "step_source.h"
#include "ble_peripheral.h"
#include "ble_peripheral_arduino.h"  // board-only concrete BLE driver
#include "step_log.h"
#include "debug_console.h"

// Running dynamics (contact time, strike pattern, oscillation, braking/overstride
// proxy, foot-recovery) is OPT-IN behind NANOWEAR_RUNNING_DYNAMICS. It is OFF by
// default so the shipped firmware is the validated single-core step-counter +
// debug step-log path (PR #14). When enabled, the firmware splits across both
// RP2040 cores (Core1 owns I2C + FIFO + gait detector; Core0 owns BLE + phone
// link) and additionally streams the raw FIFO to the GaitDetector. See below.
#ifdef NANOWEAR_RUNNING_DYNAMICS
#include <hardware/sync.h>          // RP2040 cross-core spinlock
#include "gait_detector.h"          // pulls in imu_fifo.h (FifoSource/ImuSample)
#endif

// ---------------------------------------------------------------------------
// NanoWear firmware — screenless ankle-worn fitness tracker
// ---------------------------------------------------------------------------
// DEFAULT (single-core, NANOWEAR_RUNNING_DYNAMICS undefined — PR #14 path):
//   * One core (setup/loop). Non-blocking: loop() never calls delay(); polling
//     is driven by the StateMachine + ElapsedTimer.
//   * Testable: step logic lives in Pedometer (pure) behind the IMU interface;
//     the board-only HardwareIMU is the only part that touches I2C. The
//     StateMachine is also pure, so the orchestration is host-testable.
//   * In-RAM step log: every successful poll appends {tMillis, total} to a
//     bounded ring buffer (StepLog). A USB-Serial debug console (DebugConsole)
//     can dump/extract it, no Bluetooth. This is the path the ~50-100 step
//     on-device test exercises (scripts/dump_log.py + tests/e2e).
//
// OPT-IN DUAL-CORE (NANOWEAR_RUNNING_DYNAMICS defined):
//   * Core0 (setup/loop): BLE radio, status LED, phone link. It never touches
//     I2C — it only reads the lock-guarded shared snapshot and pushes it to a
//     connected phone, keeping BLE responsive (no I2C burst blocking the radio).
//   * Core1 (setup1/loop1): OWNS the IMU. It drains the LSM6DSOX FIFO, runs the
//     GaitDetector, polls the embedded pedometer, and writes the latest
//     metrics/steps into the shared snapshot. Keeping ALL I2C on one core avoids
//     contending the single bus from two cores. The embedded (MLC) pedometer
//     still provides the authoritative low-power step count (CLAUDE.md
//     constraint); the raw FIFO only feeds the running-dynamics proxies.
// ---------------------------------------------------------------------------

// --- Object ownership (mode-independent) ---------------------------------
static HardwareIMU imu;
#ifdef NANOWEAR_SOFTWARE_PEDOMETER
static SoftwareStepSource stepSrc;        // count detector strides
#else
static HardwareStepSource stepSrc(imu);  // MLC pedometer (active)
#endif
static Pedometer pedometer(stepSrc);
static StateMachine tracker(2000);     // poll the step source every 2s
static ArduinoBlePeripheral ble;       // phone link (NINA BLE, RSC + custom)

// Bounded in-RAM step history + USB-Serial debug channel (records/dumps the
// ~50-100 step log). Owned by the I2C-owning core in each mode (Core0 single,
// Core1 dual) so dump/reset never race the bus.
static StepLog<STEP_LOG_CAPACITY> stepLog;
static DebugConsole debug(stepLog, pedometer, tracker, imu);

#ifdef NANOWEAR_RUNNING_DYNAMICS
// --- Core1-only objects (I2C + detection) -------------------------------
static GaitDetector detector(1660.f);  // 1.66 kHz FIFO ODR
static ElapsedTimer fifoTimer(15);      // Core1 FIFO-drain cadence

// --- Shared snapshot (written by Core1, read by Core0) ------------------
// Guarded by a single spinlock so a 32-bit-stable struct never tears.
static spin_lock_t* g_lock = nullptr;
static GaitMetrics g_latestGait;   // one-stride dynamics snapshot
static bool        g_gaitNew = false;
static uint16_t    g_latestSteps = 0;   // cumulative step count
static bool        g_stepsNew = false;
static bool        g_resetReq = false;    // Core0 asks Core1 to reset IMU

// Reset handler invoked by the phone over BLE (runs on Core0). It only
// *requests* a reset; Core1 performs the actual I2C write so I2C stays
// on one core. Plain function matches the C-style StepResetCallback.
void handleStepReset() {
    uint32_t save = spin_lock_blocking(g_lock);
    g_resetReq = true;
    spin_unlock(g_lock, save);
    Serial.println("[BLE] Step reset requested by phone");
}
#else
// Reset handler invoked by the phone over BLE (single core owns I2C directly).
void handleStepReset() {
    imu.resetStepCount();
    pedometer.reset();
    Serial.println("[BLE] Step reset requested by phone");
}
#endif

// ---------------------------------------------------------------------------
// setup() / loop() — always Core0
// ---------------------------------------------------------------------------
void setup() {
    Serial.begin(115200);

#ifdef NANOWEAR_RUNNING_DYNAMICS
    // Spinlock for the cross-core snapshot (init before any Core1 use). Claim an
    // SDK-arbitrated free lock number rather than hard-coding 0, which can
    // collide with pico-sdk / arduino-pico internal spinlocks and deadlock.
    g_lock = spin_lock_init(spin_lock_claim_unused(true));
#endif

    // Standard Arduino Wire bus.
    Wire.begin();

    // Verify sensor presence + configure the embedded pedometer engine.
    if (!imu.begin()) {
        Serial.println("Critical Error: Failed to find or configure LSM6DSOX.");
        return;
    }

    // BOOT -> LOGGING. pedometer.reset() pairs with the PEDO_RST_STEP command
    // issued inside imu.begin(), zeroing the firmware accumulator to match.
    pedometer.reset();
    tracker.startLogging(millis());
    Serial.println("[STATE] BOOT complete -> LOGGING");

    // Report the NINA module firmware version. BLE over the NINA-W102 requires
    // firmware >= 3.0.1 (see docs/BLE_SETUP.md); older versions make BLE.begin()
    // fail and must be flashed via the Arduino IDE / esptool before the radio
    // works. Printing it here makes the gate visible from the serial monitor.
    Serial.print("[BLE] NINA firmware: ");
    Serial.println(WiFi.firmwareVersion());

    // Start the BLE peripheral (advertise as "NanoWear"). The RGB LED is the
    // at-a-glance status: blue = advertising/discoverable, off = connected.
    pinMode(LEDB, OUTPUT);
    if (!ble.begin("NanoWear")) {
        Serial.println("[BLE] ERROR: failed to start peripheral (NINA firmware < 3.0.1?)");
        digitalWrite(LEDB, HIGH); // LED off: BLE not active
    } else {
        ble.onStepReset(handleStepReset);
        digitalWrite(LEDB, LOW); // blue ON = advertising
        Serial.println("[BLE] Advertising as 'NanoWear' (RSC 0x1814 + NanoWear steps)");
    }
}

void loop() {
    unsigned long now = millis();

    // Pump the BLE radio every tick (non-blocking). This services incoming
    // connections and any reset command the phone may have written.
    ble.poll();

    // Reflect connection state on the status LED: blue while advertising, off
    // once a phone is connected.
    digitalWrite(LEDB, ble.isConnected() ? HIGH : LOW);

#ifdef NANOWEAR_RUNNING_DYNAMICS
    // (Dual-core Core0: BLE + snapshot read only — see loop1() for the poll.)
#else
    // Throttle for the "not logging" diagnostic so it doesn't flood Serial.
    static unsigned long lastStateDiagMs = 0;

    // Debug command channel (USB Serial). Non-blocking: reads any pending byte
    // and acts on single-character commands (r/d/g/l/c/s/?) without disrupting
    // logging. Handles the user's "extract logs without Bluetooth" dev path.
    debug.pollSerial();

    // Power-optimised, non-blocking poll: act only when LOGGING and the 2s
    // window has elapsed.
    if (tracker.shouldPoll(now)) {
        tracker.markPolled(now);

        // Boot sentinel, re-emitted on every LOGGING poll. The on-device e2e
        // harness (scripts/flash-verify.sh -> tests/e2e) keys on this exact line
        // to confirm the firmware reached LOGGING. It is emitted every poll
        // (not just once at boot) so the harness captures it no matter when the
        // serial monitor attaches — a one-shot line printed before the host
        // opens the port would be lost, making the boot test flaky. See
        // tests/e2e/README.md.
        Serial.println("[NW] BOOT_OK");

        uint16_t delta = pedometer.update();
        uint16_t total = pedometer.getTotal();

        // Persist this reading to the in-RAM log only on a good read, so a
        // failed I2C read never injects a duplicate/stale timestamp.
        if (pedometer.readOk()) {
            stepLog.record(now, total);
        }

        Serial.print("[PEDOMETER] Total steps: ");
        Serial.println(total);

        if (delta > 0) {
            Serial.print("[PEDOMETER] New steps this poll: ");
            Serial.println(delta);
        }
        if (!pedometer.readOk()) {
            Serial.println("[PEDOMETER] Warning: step-count read failed (I2C error).");
        }

        // Push the latest total to any connected phone (Notify + custom char).
        ble.notifySteps(total);
    } else if (tracker.state() != TrackerState::LOGGING
               && tracker.state() != TrackerState::DEBUG
               && now - lastStateDiagMs >= 5000) {
        // We are paused outside LOGGING (e.g. BOOT after a sensor failure, or a
        // future SYNC/LOW_BATTERY state). Surface it so a stuck state is visible
        // rather than silently dropping steps.
        lastStateDiagMs = now;
        Serial.print("[STATE] Not logging (");
        Serial.print(static_cast<int>(tracker.state()));
        Serial.println("); steps are not being recorded.");
    }
#endif
}

#ifdef NANOWEAR_RUNNING_DYNAMICS
// ---------------------------------------------------------------------------
// Core1 — IMU owner: FIFO drain -> detector -> shared snapshot
// ---------------------------------------------------------------------------
void setup1() {
    // Bump I2C to 1 MHz (fast-mode+) so FIFO burst reads are quick.
    Wire.setClock(1000000);

    // Verify sensor presence + configure pedometer + FIFO. If this fails there
    // is no recovery on the prototype; Core0's status LED / diag reflects it.
    if (!imu.begin()) {
        Serial.println("Critical Error: Failed to find or configure LSM6DSOX.");
        return;
    }

    // BOOT -> LOGGING. pedometer.reset() pairs with the PEDO_RST_STEP
    // command inside imu.begin(), zeroing the firmware accumulator.
    pedometer.reset();
    tracker.startLogging(millis());
    Serial.println("[STATE] BOOT complete -> LOGGING (Core1 owns IMU)");

    // Debug command channel (USB Serial) runs on Core1 because it owns the IMU:
    // 'r' reset and 'l' dump touch the step log / I2C, so keeping it on the
    // I2C-owning core avoids a cross-core bus race. Read is non-blocking.
    // (debug.pollSerial() is called from loop1() below.)
}

void loop1() {
    static uint8_t  fifoBuf[768];   // 64 samples * 12 B
    static ImuSample samples[64];
    static float     tsBase = 0;    // fractional-ms clock, chained across bursts
    static unsigned long lastStateDiagMs = 0;

    unsigned long now = millis();

    // Surface a stuck (non-LOGGING) state so it is visible, not silent.
    // Runs on Core1 alongside the StateMachine, so no cross-core lock needed.
    if (tracker.state() != TrackerState::LOGGING
            && now - lastStateDiagMs >= 5000) {
        lastStateDiagMs = now;
        Serial.print("[STATE] Not logging (");
        Serial.print(static_cast<int>(tracker.state()));
        Serial.println("); steps are not being recorded.");
    }

    // 1) Drain the FIFO on a fixed cadence (non-blocking; no delay()).
    if (fifoTimer.hasElapsed(now)) {
        fifoTimer.reset(now);

        // FIFO_STATUS1 DIFF_FIFO is 6-bit (max ~10 samples/read at 1.66 kHz),
        // so a single read per tick would overflow the 3 KB buffer and drop
        // samples. Drain in a loop until empty; tsBase chains across reads.
        size_t filled = 0;
        while (imu.read(fifoBuf, sizeof(fifoBuf), filled) && filled > 0) {
            size_t n = decodeFifo(fifoBuf, filled,
                                   imu.fifoPattern(), imu.accelScale(),
                                   imu.gyroScale(), imu.samplePeriodMs(),
                                   tsBase, samples, 64);
            for (size_t i = 0; i < n; i++) {
                if (detector.process(samples[i])) {
                    // A stride completed: publish the snapshot + step (if software).
#ifdef NANOWEAR_SOFTWARE_PEDOMETER
                    stepSrc.onStride();
#endif
                    uint32_t save = spin_lock_blocking(g_lock);
                    g_latestGait = detector.metrics();
                    g_gaitNew    = true;
                    spin_unlock(g_lock, save);
                }
            }
        }

        // 2) Poll the step source every 2 s (cheap I2C read).
        if (tracker.shouldPoll(now)) {
            tracker.markPolled(now);

            // Boot sentinel, re-emitted on every LOGGING poll. The on-device
            // e2e harness (scripts/flash-verify.sh -> tests/e2e) keys on this
            // exact line to confirm the firmware reached LOGGING. It is emitted
            // every poll (not just once at boot) so the harness captures it no
            // matter when the serial monitor attaches — a one-shot line printed
            // before the host opens the port would be lost, making the boot
            // test flaky. See tests/e2e/README.md.
            Serial.println("[NW] BOOT_OK");

            uint16_t delta = pedometer.update();
            uint16_t total = pedometer.getTotal();
            Serial.print("[PEDOMETER] Total steps: ");
            Serial.println(total);
            if (delta > 0) {
                Serial.print("[PEDOMETER] New steps this poll: ");
                Serial.println(delta);
            }
            if (!pedometer.readOk()) {
                Serial.println("[PEDOMETER] Warning: step-count read failed (I2C error).");
            }
            // Persist to the in-RAM step log (owned by Core1) for the debug
            // dump; then publish the total to Core0's snapshot for BLE notify.
            if (pedometer.readOk()) stepLog.record(now, total);
            uint32_t save = spin_lock_blocking(g_lock);
            g_latestSteps = total;
            g_stepsNew    = true;
            spin_unlock(g_lock, save);
        }

        // 3) Honour a phone-requested reset (I2C write, Core1 only).
        bool doReset = false;
        uint32_t save = spin_lock_blocking(g_lock);
        if (g_resetReq) { doReset = true; g_resetReq = false; }
        spin_unlock(g_lock, save);
        if (doReset) {
            imu.resetStepCount();
            pedometer.reset();
            Serial.println("[PEDOMETER] Reset performed (Core1).");
        }
    }

    // USB-Serial debug channel (Core1 owns the IMU/log it touches).
    debug.pollSerial();
}
#endif
