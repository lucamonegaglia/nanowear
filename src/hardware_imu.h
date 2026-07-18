#ifndef NANOWEAR_HARDWARE_IMU_H
#define NANOWEAR_HARDWARE_IMU_H

#include <Arduino.h>
#include <Wire.h>
#include "imu.h"
#include "imu_fifo.h"   // FifoSource interface + FifoPattern
#include "lsm6dsox_reg.h"   // STMicroelectronics standard-C LSM6DSOX driver

// ---------------------------------------------------------------------------
// HardwareIMU — LSM6DSOX driver for the Nano RP2040 Connect
// ---------------------------------------------------------------------------
// Concrete IMUSensor. It drives the sensor's embedded (hardware) pedometer
// through STMicroelectronics' standard-C driver (lsm6dsox_reg.c, vendored in
// lib/lsm6dsox) over the on-board Wire I2C bus, and (when running dynamics is
// enabled) streams the raw 6-axis FIFO (accel + gyro at high ODR) so the gait
// detector can derive running dynamics. Compiled only for the real board; the
// native test env never sees this file, which is why the testable logic lives
// behind the IMU interface and the FifoSource seam.
//
// The DEFAULT build's authoritative step count comes from the custom software
// step detector, fed by the raw FIFO stream (the embedded MLC pedometer proved
// unreliable on hardware and is configured here as a legacy/optional source).
// The FIFO is therefore ALWAYS configured in begin() so the software detector
// has a stream; the opt-in running-dynamics gait detector also consumes it.
//
// The ST driver is reached through a stmdev_ctx_t whose read/write/delay
// callbacks (defined in hardware_imu.cpp) talk to Wire at the IMU's 7-bit
// address 0x6A. The driver owns all register/embedded-function-bank details;
// this class only sequences the high-level configuration steps.
// ---------------------------------------------------------------------------
class HardwareIMU : public IMUSensor, public FifoSource {
public:
    HardwareIMU();

    // Initialise the sensor: presence check + embedded pedometer. When
    // NANOWEAR_RUNNING_DYNAMICS is set, the FIFO stream is also configured.
    // Returns true only if the required steps succeed.
    bool begin() override;

    // Read the cumulative step count. Returns false on a transport error.
    bool readStepCount(uint16_t& out) override;

    // Zero the embedded pedometer's step counter (IMUSensor interface), leaving
    // the algorithm enabled. Returns true on success; used by the debug console
    // and the BLE "reset" command.
    bool resetStepCount() override;

    // --- FifoSource (raw burst reader) -----------------------------------
    // Drain the LSM6DSOX FIFO into `out` (up to `cap` bytes). Returns
    // true on a successful drain; `filled` is the byte count (0 if empty).
    bool read(uint8_t* out, size_t cap, size_t& filled) override;

    // Scale / period the host-side decoder needs (board-only, set in initFifo).
    float accelScale() const { return aScale_; }
    float gyroScale()  const { return gScale_; }
    float samplePeriodMs() const { return dtMs_; }
    const FifoPattern& fifoPattern() const { return fifoPattern_; }

    // Debug accessor for on-device FIFO bring-up: read any register.
    uint8_t debugReadReg(uint8_t reg) { return readRegister(reg); }

    // LSM6DSOX I2C device address (7-bit). Public so the file-scope ST-driver
    // platform callbacks (which are not class members) can reach it; the ST
    // header's LSM6DSOX_I2C_ADD_L is the 8-bit form 0xD5 = 0x6A << 1, which
    // Arduino Wire does not want.
    static constexpr uint8_t LSM6DSOX_I2C_ADDR = 0x6A;

private:
    // Write a single byte to an IMU register over I2C. Returns true on ACK.
    // Used by the FIFO path; the pedometer path goes through the ST driver.
    bool writeRegister(uint8_t reg, uint8_t value);

    // Read a single byte from an IMU register over I2C. Returns 0 on NACK.
    uint8_t readRegister(uint8_t reg);

    // Configure the LSM6DSOX embedded pedometer engine via the ST driver
    // (called from begin()). Returns true on success.
    bool initHardwarePedometer();

    // Configure the FIFO for accel+gyro streaming at 1.66 kHz (continuous
    // mode). Also caches the scale factors + sample period used by decodeFifo.
    // Called unconditionally from begin() — the software step detector (the
    // default step source) and the opt-in gait detector both need this stream.
    bool initFifo();

    // --- FIFO register map (user bank; no FUNC_CFG_ACCESS switch) --------
    static constexpr uint8_t CTRL1_XL    = 0x10;  // accel ODR + full-scale
    static constexpr uint8_t CTRL2_G     = 0x11;  // gyro  ODR + full-scale
    static constexpr uint8_t FIFO_CTRL1   = 0x07;  // watermark (low byte)
    static constexpr uint8_t FIFO_CTRL3   = 0x09;  // accel/gyro decimation
    static constexpr uint8_t FIFO_CTRL5   = 0x0B;  // FIFO mode
    static constexpr uint8_t FIFO_STATUS1 = 0x3A;  // DIFF_FIFO (unread words)
    static constexpr uint8_t FIFO_DATA_OUT_L = 0x3E;  // FIFO data (burst read)

    // ST standard-C driver context (read/write/delay callbacks -> Wire).
    stmdev_ctx_t dev_ctx_;

    // Scale factors at the chosen full-scale (LSB -> physical):
    float aScale_ = 0.000122f;   // ±4g  -> 0.122 mg/LSB  = 0.000122 g/LSB
    float gScale_ = 0.070f;       // ±2000 dps -> 70 mdps/LSB = 0.070 dps/LSB
    float dtMs_   = 1000.f / 1660.f;  // sample period at 1.66 kHz
    FifoPattern fifoPattern_;          // accel + gyro, no timestamp (12 B/sample)
};

#endif // NANOWEAR_HARDWARE_IMU_H
