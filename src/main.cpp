#include <Arduino.h>
#include <WiFiNINA.h>            // onboard RGB LED (LEDR/LEDG/LEDB); active-low
#include "hardware/sync.h"          // RP2040 cross-core spinlock
#include "hardware_imu.h"
#include "pedometer.h"
#include "state_machine.h"
#include "step_source.h"
#include "gait_detector.h"          // pulls in imu_fifo.h (FifoSource/ImuSample)
#include "ble_peripheral.h"
#include "ble_peripheral_arduino.h"  // board-only concrete BLE driver

// ---------------------------------------------------------------------------
// NanoWear firmware — screenless ankle-worn fitness tracker
// ---------------------------------------------------------------------------
// DUAL-CORE split (both RP2040 cores, per "use the board's resources"):
//   * Core0 (setup/loop): BLE radio, status LED, and the *phone link*.
//       It never touches I2C — it only reads the lock-guarded shared
//       snapshot and pushes it to a connected phone. This keeps BLE
//       responsive (no I2C burst blocking the radio).
//   * Core1 (setup1/loop1): OWNS the IMU. It drains the LSM6DSOX
//       FIFO, runs the GaitDetector, polls the embedded pedometer, and
//       writes the latest metrics/steps into the shared snapshot. Keeping ALL
//       I2C on one core avoids contending the single bus from two cores.
//
// Non-blocking: no delay() anywhere. Core1 polls the FIFO on an
// ElapsedTimer; Core0 pumps BLE every tick.
//
// TIER A running dynamics: the embedded (MLC) pedometer still provides
// the authoritative low-power step count (AGENTS.md constraint kept); the
// raw FIFO stream feeds the GaitDetector for contact time, strike pattern,
// oscillation, braking/overstride proxy and foot-recovery. The step
// SOURCE is swappable (step_source.h): today it is the hardware
// pedometer; flip NANOWEAR_SOFTWARE_PEDOMETER to count gait
// strides instead — no detector rewrite.
// ---------------------------------------------------------------------------

// --- Core1 owns these (I2C + detection) ---------------------------
static HardwareIMU imu;
#ifdef NANOWEAR_SOFTWARE_PEDOMETER
static SoftwareStepSource stepSrc;        // count detector strides
#else
static HardwareStepSource stepSrc(imu);  // MLC pedometer (active)
#endif
static Pedometer pedometer(stepSrc);
static StateMachine tracker(2000);     // poll the step source every 2s
static GaitDetector detector(1660.f);  // 1.66 kHz FIFO ODR
static ElapsedTimer fifoTimer(15);      // Core1 FIFO-drain cadence

// --- Core0 owns these (BLE + phone link) --------------------------
static ArduinoBlePeripheral ble;

// --- Shared snapshot (written by Core1, read by Core0) ----------
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
    uint32_t save;
    save = spin_lock_blocking(g_lock);
    g_resetReq = true;
    spin_unlock(g_lock, save);
    Serial.println("[BLE] Step reset requested by phone");
}

// Throttle for the "not logging" diagnostic so it doesn't flood Serial.
static unsigned long lastStateDiagMs = 0;

// ---------------------------------------------------------------------------
// Core0 — BLE, LED, phone link (no I2C here)
// ---------------------------------------------------------------------------
void setup() {
    Serial.begin(115200);

    // Spinlock for the cross-core snapshot (init before any core1 use).
    g_lock = spin_lock_init(0);

    // Standard Arduino Wire bus (Core1 drives it; just initialise here).
    Wire.begin();

    // Report the NINA module firmware version. BLE over the NINA-W102
    // requires firmware >= 3.0.1 (see docs/BLE_SETUP.md).
    Serial.print("[BLE] NINA firmware: ");
    Serial.println(WiFi.firmwareVersion());

    // Start the BLE peripheral (advertise as "NanoWear"). The RGB LED is
    // the at-a-glance status: blue = advertising, off = connected.
    pinMode(LEDB, OUTPUT);
    if (!ble.begin("NanoWear")) {
        Serial.println("[BLE] ERROR: failed to start peripheral (NINA firmware < 3.0.1?)");
        digitalWrite(LEDB, HIGH); // LED off: BLE not active
    } else {
        ble.onStepReset(handleStepReset);
        digitalWrite(LEDB, LOW); // blue ON = advertising
        Serial.println("[BLE] Advertising as 'NanoWear' (RSC 0x1814 + NanoWear steps/dynamics)");
    }
}

void loop() {
    // Pump the BLE radio every tick (non-blocking).
    ble.poll();

    // Reflect connection state on the status LED.
    digitalWrite(LEDB, ble.isConnected() ? HIGH : LOW);

    // Push the latest metrics to any connected phone. Reads are lock-guarded
    // and throttled by the "new" flags Core1 sets, so we only notify
    // on fresh data (and never block on I2C).
    if (ble.isConnected()) {
        GaitMetrics g;
        uint16_t steps = 0;
        bool gaitNew = false, stepsNew = false;
        uint32_t save;
        save = spin_lock_blocking(g_lock);
        if (g_gaitNew)  { g = g_latestGait;  g_gaitNew  = false; gaitNew  = true; }
        if (g_stepsNew) { steps = g_latestSteps; g_stepsNew = false; stepsNew = true; }
        spin_unlock(g_lock, save);

        if (gaitNew)  ble.notifyGait(g);
        if (stepsNew) ble.notifySteps(steps);
    }

    // Surface a stuck (non-LOGGING) state so it is visible, not silent.
    if (tracker.state() != TrackerState::LOGGING
            && millis() - lastStateDiagMs >= 5000) {
        lastStateDiagMs = millis();
        Serial.print("[STATE] Not logging (");
        Serial.print(static_cast<int>(tracker.state()));
        Serial.println("); steps are not being recorded.");
    }
}

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

    // Buffer for one FIFO burst: 64 samples * 12 bytes = 768 bytes.
    // Allocated once on the stack per drain via the static inside loop1().
}

void loop1() {
    static uint8_t  fifoBuf[768];   // 64 samples * 12 B
    static ImuSample samples[64];
    static uint32_t  tsBase = 0;

    unsigned long now = millis();

    // 1) Drain the FIFO on a fixed cadence (non-blocking; no delay()).
    if (fifoTimer.hasElapsed(now)) {
        fifoTimer.reset(now);

        size_t filled = 0;
        if (imu.read(fifoBuf, sizeof(fifoBuf), filled) && filled > 0) {
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
                    uint32_t save;
                    save = spin_lock_blocking(g_lock);
                    g_latestGait = detector.metrics();
                    g_gaitNew    = true;
                    spin_unlock(g_lock, save);
                }
            }
        }

        // 2) Poll the step source every 2 s (cheap I2C read).
        if (tracker.shouldPoll(now)) {
            tracker.markPolled(now);
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
            uint32_t save;
            save = spin_lock_blocking(g_lock);
            g_latestSteps = total;
            g_stepsNew    = true;
            spin_unlock(g_lock, save);
        }

        // 3) Honour a phone-requested reset (I2C write, Core1 only).
        bool doReset = false;
        uint32_t save;
        save = spin_lock_blocking(g_lock);
        if (g_resetReq) { doReset = true; g_resetReq = false; }
        spin_unlock(g_lock, save);
        if (doReset) {
            imu.resetPedometerSteps();
            pedometer.reset();
            Serial.println("[PEDOMETER] Reset performed (Core1).");
        }
    }
}
