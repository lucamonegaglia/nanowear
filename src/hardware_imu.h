#ifndef NANOWEAR_HARDWARE_IMU_H
#define NANOWEAR_HARDWARE_IMU_H

#include <Arduino.h>
#include <Wire.h>
#include <Arduino_LSM6DSOX.h>
#include "imu.h"
#include "step_codec.h"
#include "imu_fifo.h"   // FifoSource interface + FifoPattern

// ---------------------------------------------------------------------------
// HardwareIMU — LSM6DSOX driver for the Nano RP2040 Connect
// ---------------------------------------------------------------------------
// Concrete IMU implementation. It performs the register-level configuration of
// the sensor's embedded (hardware) pedometer AND (when running dynamics is
// enabled) streams the raw 6-axis FIFO (accel + gyro at high ODR) so the gait
// detector can derive running dynamics. Compiled only for the real board; the
// native test env never sees this file, which is why the testable logic lives
// behind the IMU interface and the FifoSource seam.
//
// The embedded pedometer stays the authoritative low-power step counter in
// BOTH modes (CLAUDE.md constraint). The FIFO is an ADDITIONAL stream consumed
// only by the gait detector; it is initialised solely under
// NANOWEAR_RUNNING_DYNAMICS so the default single-core build matches the
// validated step-counting path exactly (no ODR change to the pedometer feed).
// ---------------------------------------------------------------------------
class HardwareIMU : public IMUSensor, public FifoSource {
public:
    // Initialise the sensor: presence check + embedded pedometer. When
    // NANOWEAR_RUNNING_DYNAMICS is set, the FIFO stream is also configured.
    // Returns true only if the required steps succeed.
    bool begin() override;

    // Read the cumulative step count. Returns false on a transport error.
    bool readStepCount(uint16_t& out) override;

    // Zero the hardware step counter in place (PEDO_RST_STEP), without
    // re-running the full pedometer init. Used when a phone requests a reset
    // over BLE. Mirrors step 3 of initHardwarePedometer.
    void resetPedometerSteps();

    // Zero the embedded pedometer's step counter (IMUSensor interface), leaving
    // the algorithm enabled. Returns true on success; used by the debug console.
    bool resetStepCount() override;

    // True if the LSM6DSOX PEDO_EN bit was observed set after init (walk-free
    // proof the embedded pedometer engine is actually enabled on the part).
    bool pedometerEnabled() const { return pedoEnabled_; }

    // Diagnostic: read back the pedometer-enable register (EMB_FUNC_EN_A) and
    // confirm the PEDO_EN bit (0x08) is set on the part.
    void debugProbe();

    // --- Live raw motion (debug / analysis) ---------------------------------
    // Read the latest accelerometer (g) and gyroscope (deg/s) samples directly
    // from the sensor, bypassing the embedded pedometer's thresholding/debounce.
    // Used by the DEBUG step-log so the raw motion trace can be inspected
    // independently of the step count. Returns true on a successful read.
    bool readAcceleration(float& x, float& y, float& z);
    bool readGyroscope(float& x, float& y, float& z);

    // --- FifoSource (raw burst reader) -----------------------------------
    // Drain the LSM6DSOX FIFO into `out` (up to `cap` bytes). Returns
    // true on a successful drain; `filled` is the byte count (0 if empty).
    bool read(uint8_t* out, size_t cap, size_t& filled) override;

    // Scale / period the host-side decoder needs (board-only, set in initFifo).
    float accelScale() const { return aScale_; }
    float gyroScale()  const { return gScale_; }
    float samplePeriodMs() const { return dtMs_; }
    const FifoPattern& fifoPattern() const { return fifoPattern_; }

private:
    // Write a single byte to an IMU register over I2C. Returns true on ACK.
    bool writeRegister(uint8_t reg, uint8_t value);

    // Read a single byte from an IMU register over I2C. Returns 0 on NACK.
    uint8_t readRegister(uint8_t reg);

    // Configure the LSM6DSOX embedded pedometer engine (called from begin()).
    bool initHardwarePedometer();

    // Configure the FIFO for accel+gyro streaming at 1.66 kHz (continuous
    // mode). Also caches the scale factors + sample period used by decodeFifo.
    // Only invoked under NANOWEAR_RUNNING_DYNAMICS.
    bool initFifo();

    // Open / close the Embedded Functions configuration register bank, with the
    // pedometer registers' page (Page 0) selected. All pedometer accesses
    // (EMB_FUNC_EN_A, EMB_FUNC_SRC, STEP_COUNTER) live in Page 0 of the
    // embedded-functions bank; selecting it explicitly means a read can never
    // land on Page 1 (where PEDO_CMD_REG / PEDO_DEB_STEPS_CONF live).
    bool openFuncBank();
    bool closeFuncBank();

    // Write PEDO_DEB_STEPS_CONF (advanced Page 1) via the indirect PAGE mechanism
    // to set the pedometer debounce threshold (min consistent steps before the
    // counter increments). Debug build only — lowers it so short on-device
    // tests trip counting without a full walk.
    bool configurePedometerDebounce(uint8_t debounceSteps);

    static constexpr uint8_t SUBADDR_AUTO_INC = 0x80; // MSB of sub-address = burst read

    // LSM6DSOX I2C device address
    static constexpr uint8_t LSM6DSOX_I2C_ADDR = 0x6A;

    // Embedded Functions register map
    static constexpr uint8_t FUNC_CFG_ACCESS      = 0x01;
    static constexpr uint8_t PAGE_SEL             = 0x02; // embedded-func page select; 0 = Page 0 (pedometer regs)
    static constexpr uint8_t EMB_FUNC_EN_A        = 0x04; // PEDO_EN bit (0x08) enables the pedometer
    static constexpr uint8_t PEDO_CMD_REG         = 0x83; // pedometer config reg (CARRY_COUNT_EN / FP_REJECTION_EN / AD_DET_EN);
                                                        // lives in Page 1 — left at its sensible defaults, not touched here
    static constexpr uint8_t EMB_FUNC_SRC         = 0x64; // PEDO_RST_STEP (bit 7) resets the step counter
    static constexpr uint8_t PEDO_RST_STEP        = 0x80; // bit 7 of EMB_FUNC_SRC(0x64): reset the step count
    static constexpr uint8_t EMB_FUNC_INIT_A      = 0x66; // STEP_DET_INIT (bit 3) starts the pedometer algorithm
    static constexpr uint8_t STEP_DET_INIT        = 0x08; // bit 3 of EMB_FUNC_INIT_A(0x66): pulse to init the algo
    static constexpr uint8_t WHO_AM_I             = 0x0F; // LSM6DSOX id = 0x6C (user bank, no func-bank switch)
    static constexpr uint8_t INT1_CTRL            = 0x0D;
    static constexpr uint8_t EMB_FUNC_INT1        = 0x0A; // INT1 step-detection routing (embedded-func bank, Page 0)

    // Step counter register offsets (embedded-functions Page 0, FUNC_CFG_EN = 1).
    // Per LSM6DSOX DS (DM00571818) §13.42 these are 62h/63h — NOT 4Bh/4Ch
    // (those are the OIS gyro outputs UI_OUTX_H_G_OIS / UI_OUTY_L_G_OIS, which
    // is why the old code always read 0 steps: it was reading gyro data).
    static constexpr uint8_t STEP_COUNTER_L       = 0x62;
    static constexpr uint8_t STEP_COUNTER_H       = 0x63;

    // FUNC_CFG_ACCESS bank-select magic values
    static constexpr uint8_t FUNC_CFG_BANK        = 0x80; // FUNC_CFG_EN=1: access embedded func config
    static constexpr uint8_t FUNC_CFG_BANK_CLOSE  = 0x00; // return to default (user) page

    // Embedded advanced-features Page 1 access (indirect PAGE mechanism, DS §14).
    // PEDO_DEB_STEPS_CONF / PEDO_CMD_REG live here and are written by pointing
    // PAGE_ADDRESS at the target and writing PAGE_VALUE while PAGE_WRITE is set.
    static constexpr uint8_t PAGE_ADDRESS         = 0x08; // PAGE_ADDR[7:0]: target reg in selected page
    static constexpr uint8_t PAGE_VALUE           = 0x09; // PAGE_VALUE[7:0]: data to write
    static constexpr uint8_t PAGE_RW              = 0x17; // advanced-page R/W control
    static constexpr uint8_t PAGE_WRITE_BIT       = 0x40; // bit 6 of PAGE_RW: enable page writes
    static constexpr uint8_t PAGE_SEL_PAGE1       = 0x01; // PAGE_SEL[3:0]=0001 -> advanced features Page 1
    static constexpr uint8_t PEDO_DEB_STEPS_CONF_ADDR = 0x84; // PEDO_DEB_STEPS_CONF addr (Page 1)
    // Debug build only: count after this many consistent steps (default 0x0A=10).
    static constexpr uint8_t PEDO_DEB_STEPS_DEBUG = 0x02;

    // --- FIFO register map (user bank; no FUNC_CFG_ACCESS switch) --------
    static constexpr uint8_t CTRL1_XL    = 0x10;  // accel ODR + full-scale
    static constexpr uint8_t CTRL2_G     = 0x11;  // gyro  ODR + full-scale
    static constexpr uint8_t FIFO_CTRL1   = 0x07;  // watermark (low byte)
    static constexpr uint8_t FIFO_CTRL3   = 0x09;  // accel/gyro decimation
    static constexpr uint8_t FIFO_CTRL5   = 0x0B;  // FIFO mode
    static constexpr uint8_t FIFO_STATUS1 = 0x3A;  // DIFF_FIFO (unread words)
    static constexpr uint8_t FIFO_DATA_OUT_L = 0x3E;  // FIFO data (burst read)

    // Scale factors at the chosen full-scale (LSB -> physical):
    float aScale_ = 0.000122f;   // ±4g  -> 0.122 mg/LSB  = 0.000122 g/LSB
    float gScale_ = 0.070f;       // ±2000 dps -> 70 mdps/LSB = 0.070 dps/LSB
    float dtMs_   = 1000.f / 1660.f;  // sample period at 1.66 kHz
    FifoPattern fifoPattern_;          // accel + gyro, no timestamp (12 B/sample)
    bool pedoEnabled_ = false;         // PEDO_EN observed after init
};

#endif // NANOWEAR_HARDWARE_IMU_H
