#ifndef NANOWEAR_HARDWARE_IMU_H
#define NANOWEAR_HARDWARE_IMU_H

#include <Arduino.h>
#include <Wire.h>
#include <Arduino_LSM6DSOX.h>
#include "imu.h"
#include "step_codec.h"

// ---------------------------------------------------------------------------
// HardwareIMU — LSM6DSOX driver for the Nano RP2040 Connect
// ---------------------------------------------------------------------------
// Concrete IMU implementation. It performs the register-level configuration of
// the sensor's embedded (hardware) pedometer and reads back the cumulative
// step count. Compiled only for the real board; the native test env never sees
// this file, which is why the testable logic lives behind the IMU interface.
// ---------------------------------------------------------------------------
class HardwareIMU : public IMUSensor {
public:
    bool begin() override;
    void writeRegister(uint8_t reg, uint8_t value) override;
    uint8_t readRegister(uint8_t reg) override;
    uint16_t readStepCount() override;

    // Configure the LSM6DSOX embedded pedometer engine (call once after begin()).
    // Public so main.cpp can drive the explicit BOOT -> LOGGING sequence; a
    // later revision may fold this into begin().
    void initHardwarePedometer();

private:

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
};

#endif // NANOWEAR_HARDWARE_IMU_H
