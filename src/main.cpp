#include <Arduino.h>
#include "hardware_imu.h"
#include "pedometer.h"
#include "state_machine.h"
#include "step_source.h"

// Communication mode is chosen at build time via the COM_MODE macro (see
// platformio.ini). Exactly one of COM_MODE_BLE / COM_MODE_DEBUG is defined; the
// unused path is not compiled, so the two modes are mutually exclusive — the
// BLE radio is absent from the DEBUG build and the ring-buffer dump is not
// *activated* in the BLE build. Switch modes by flashing the other env (no NINA
// firmware reflash needed, only the sketch changes).
//
// Running dynamics (NANOWEAR_RUNNING_DYNAMICS) is a BLE-only sub-feature: it
// splits the firmware across both RP2040 cores with BLE on Core0, so it is only
// valid together with COM_MODE_BLE. In DEBUG mode (no BLE) the dual-core path is
// compiled out and the ring buffer lives on the single core instead.
#if defined(COM_MODE_BLE)
  #include <WiFiNINA.h>            // RGB LED (LEDR/LEDG/LEDB); active-low
  #include "ble_peripheral.h"
  #include "ble_peripheral_arduino.h"  // board-only concrete BLE driver
  // Running dynamics keeps the in-RAM step log + USB-Serial dump on Core1, so
  // it needs these headers even in the BLE comm mode.
  #if defined(NANOWEAR_RUNNING_DYNAMICS)
    #include "step_log.h"
    #include "debug_console.h"
  #endif
#elif defined(COM_MODE_DEBUG)
  #include "step_log.h"
  #include "debug_console.h"
#else
  #error "COM_MODE must be defined: build with -D COM_MODE_BLE or -D COM_MODE_DEBUG (see platformio.ini)"
#endif

// Running dynamics (contact time, strike pattern, oscillation, braking/overstride
// proxy, foot-recovery) is OPT-IN behind NANOWEAR_RUNNING_DYNAMICS and only valid
// with the BLE comm mode. It is OFF by default so the shipped firmware is the
// validated single-core step-counter + debug step-log path (PR #14). When enabled,
// the firmware splits across both RP2040 cores (Core1 owns I2C + FIFO + gait
// detector; Core0 owns BLE + phone link) and additionally streams the raw FIFO to
// the GaitDetector. See below.
#if defined(NANOWEAR_RUNNING_DYNAMICS) && defined(COM_MODE_BLE)
#include <hardware/sync.h>          // RP2040 cross-core spinlock
#include "gait_detector.h"          // pulls in imu_fifo.h (FifoSource/ImuSample)
#endif

// ---------------------------------------------------------------------------
// NanoWear firmware — screenless ankle-worn fitness tracker
// ---------------------------------------------------------------------------
// Communication mode (compile-time, via COM_MODE):
//   * BLE  : stream Running Speed & Cadence (RSC 0x1814) + raw steps to a phone
//            over the NINA BLE radio (BlePeripheral). Default build.
//   * DEBUG: append every successful poll to a bounded in-RAM ring buffer
//            (StepLog) and dump/extract it over USB Serial via DebugConsole —
//            no Bluetooth. The board has no filesystem in its toolchain, so this
//            ring buffer is the "saved logs".
//
// DEFAULT (single-core, NANOWEAR_RUNNING_DYNAMICS undefined — PR #14 path):
//   * One core (setup/loop). Non-blocking: loop() never calls delay(); polling
//     is driven by the StateMachine + ElapsedTimer.
//   * Testable: step logic lives in Pedometer (pure) behind the IMU interface;
//     the board-only HardwareIMU is the only part that touches I2C. The
//     StateMachine is also pure, so the orchestration is host-testable.
//
// OPT-IN DUAL-CORE (NANOWEAR_RUNNING_DYNAMICS + COM_MODE_BLE):
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

// Raw motion-trace sample period (ms) for the DEBUG ring buffer. 40 ms = 25 Hz,
// enough to resolve foot-strike transients while keeping the capture window
// within STEP_LOG_CAPACITY (1024 * 40 ms ≈ 41 s). Lower = higher rate / shorter
// window. Tune freely.
constexpr unsigned long RAW_SAMPLE_MS = 40;

// --- Object ownership (mode-independent) ---------------------------------
static HardwareIMU imu;
#ifdef NANOWEAR_SOFTWARE_PEDOMETER
static SoftwareStepSource stepSrc;        // count detector strides
#else
static HardwareStepSource stepSrc(imu);  // MLC pedometer (active)
#endif
static Pedometer pedometer(stepSrc);
static StateMachine tracker(2000);     // poll the step source every 2s

#if defined(COM_MODE_BLE)
static ArduinoBlePeripheral ble;       // phone link (NINA BLE, RSC + custom)
// In BLE mode the debug dump is normally absent (mutual exclusivity). It is
// compiled in only when running dynamics is enabled, because Core1 owns the
// ring buffer + USB-Serial channel there.
#if defined(NANOWEAR_RUNNING_DYNAMICS)
static StepLog<STEP_LOG_CAPACITY> stepLog;
static DebugConsole debug(stepLog, pedometer, tracker, imu);
#endif
#elif defined(COM_MODE_DEBUG)
// Bounded in-RAM step history + USB-Serial debug channel (records/dumps the
// ~50-100 step log). Owned by the single core in DEBUG mode.
static StepLog<STEP_LOG_CAPACITY> stepLog;
static DebugConsole debug(stepLog, pedometer, tracker, imu);
#endif

#if defined(NANOWEAR_RUNNING_DYNAMICS) && defined(COM_MODE_BLE)
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
// Also compiled for DEBUG mode (no phone link, so it never fires there).
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

    // Communication mode banner — lets the on-device e2e harness (run_e2e.py)
    // select the right per-feature tests without knowing which env was flashed.
#if defined(COM_MODE_BLE)
    Serial.println("[MODE] BLE");
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
#elif defined(COM_MODE_DEBUG)
    Serial.println("[MODE] DEBUG");
#endif
}

void loop() {
    unsigned long now = millis();

#if defined(COM_MODE_BLE)
    // Pump the BLE radio every tick (non-blocking). This services incoming
    // connections and any reset command the phone may have written.
    ble.poll();

    // Reflect connection state on the status LED: blue while advertising, off
    // once a phone is connected.
    digitalWrite(LEDB, ble.isConnected() ? HIGH : LOW);
#elif defined(COM_MODE_DEBUG)
    // No BLE radio in DEBUG mode; nothing to pump here.
#endif

#ifdef NANOWEAR_RUNNING_DYNAMICS
    // (Dual-core Core0: BLE + snapshot read only — see loop1() for the poll.)
#else
    // Throttle for the "not logging" diagnostic so it doesn't flood Serial.
    static unsigned long lastStateDiagMs = 0;

#if defined(COM_MODE_DEBUG)
    // Debug command channel (USB Serial). Non-blocking: reads any pending byte
    // and acts on single-character commands (r/d/g/l/c/s/?) without disrupting
    // logging. Handles the user's "extract logs without Bluetooth" dev path.
    debug.pollSerial();

    // Raw motion trace — independent of the pedometer's threshold/debounce.
    // While LOGGING, sample the live accel + gyro at RAW_SAMPLE_MS and append to
    // the ring buffer. The step total only changes every 2 s (Pedometer poll),
    // so it is carried through unchanged between polls. This lets us verify gait
    // from the raw signal instead of trusting the hardware count.
    static ElapsedTimer rawTimer(RAW_SAMPLE_MS);
    if (tracker.state() == TrackerState::LOGGING && rawTimer.hasElapsed(now)) {
        rawTimer.reset(now);
        float ax, ay, az, gx, gy, gz;
        if (imu.readAcceleration(ax, ay, az) && imu.readGyroscope(gx, gy, gz)) {
            stepLog.record(now, pedometer.getTotal(),
                           ax, ay, az, gx, gy, gz);
        }
    }
#endif

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

        // Communication-mode heartbeat (mirrors [NW] BOOT_OK): re-emitted every
        // poll so the e2e harness detects which build is flashed even if it
        // attached after the one-shot boot banner.
#if defined(COM_MODE_BLE)
        Serial.println("[MODE] BLE");
        // While advertising (no phone connected), re-emit the advertising
        // confirmation as a heartbeat so the e2e harness can confirm the radio
        // came up even if it attached after the one-shot boot banner. It stops
        // once a phone connects (the [BLE] Advertising line then disappears).
        if (!ble.isConnected()) {
            Serial.println("[BLE] Advertising as 'NanoWear' (RSC 0x1814 + NanoWear steps)");
        }
#elif defined(COM_MODE_DEBUG)
        Serial.println("[MODE] DEBUG");
#endif

        uint16_t delta = pedometer.update();
        uint16_t total = pedometer.getTotal();

#if defined(COM_MODE_DEBUG)
        // Walk-free verification: confirm the pedometer engine is actually
        // enabled on the part (proves the register fix without the board
        // strapped to an ankle).
        Serial.print("[PEDOMETER] PEDO_EN: ");
        Serial.println(imu.pedometerEnabled() ? "ON" : "OFF");
        // NOTE: the raw motion trace (accel + gyro) is now recorded by the
        // high-rate sampler above, not here, so the buffer holds a continuous
        // signal rather than one sample per 2 s step poll.
#endif

        Serial.print("[PEDOMETER] Total steps: ");
        Serial.println(total);

        if (delta > 0) {
            Serial.print("[PEDOMETER] New steps this poll: ");
            Serial.println(delta);
        }
        if (!pedometer.readOk()) {
            Serial.println("[PEDOMETER] Warning: step-count read failed (I2C error).");
        }

#if defined(COM_MODE_BLE)
        // Push the latest total to any connected phone (Notify + custom char).
        ble.notifySteps(total);
#endif
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

#if defined(NANOWEAR_RUNNING_DYNAMICS) && defined(COM_MODE_BLE)
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
