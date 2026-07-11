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
// the sensor's embedded (hardware) pedometer AND streams the raw 6-axis FIFO
// (accel + gyro at high ODR) so the gait detector can derive running
// dynamics. Compiled only for the real board; the native test env never sees
// this file, which is why the testable logic lives behind the IMU interface
// and the FifoSource seam.
// ---------------------------------------------------------------------------
class HardwareIMU : public IMUSensor, public FifoSource {
public:
    // Initialise the sensor: presence check, embedded pedometer, and FIFO.
    // Returns true only if all three succeed.
    bool begin() override;

    // Read the cumulative step count. Returns false on a transport error.
    bool readStepCount(uint16_t& out) override;

    // Zero the hardware step counter in place (PEDO_RST_STEP), without
    // re-running the full pedometer init. Used when a phone requests a reset
    // over BLE. Mirrors step 3 of initHardwarePedometer.
    void resetPedometerSteps();

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
    bool initFifo();

    // Open / close the Embedded Functions configuration register bank.
    bool openFuncBank();
    bool closeFuncBank();

    static constexpr uint8_t SUBADDR_AUTO_INC = 0x80; // MSB of sub-address = burst read

    // LSM6DSOX I2C device address
    static constexpr uint8_t LSM6DSOX_I2C_ADDR = 0x6A;

    // Embedded Functions register map
    static constexpr uint8_t FUNC_CFG_ACCESS      = 0x01;
    static constexpr uint8_t EMB_FUNC_EN_A        = 0x04;
    static constexpr uint8_t PEDO_CMD_REG         = 0x83;
    static constexpr uint8_t INT1_CTRL            = 0x0D;
    static constexpr uint8_t EMB_FUNC_INT1        = 0x0A;

    // Step counter register offsets (Page 0 of Embedded Advanced Registers)
    static constexpr uint8_t STEP_COUNTER_L       = 0x4B;
    static constexpr uint8_t STEP_COUNTER_H       = 0x4C;

    // FUNC_CFG_ACCESS bank-select magic values
    static constexpr uint8_t FUNC_CFG_BANK        = 0x80; // access embedded func config
    static constexpr uint8_t ADV_INT_BANK         = 0x40; // access advanced interrupt page
    static constexpr uint8_t FUNC_CFG_BANK_CLOSE  = 0x00; // return to default page

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
};

#endif // NANOWEAR_HARDWARE_IMU_H
