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
    // Initialise the sensor and configure the embedded pedometer engine.
    // Returns true only if both the presence check and the pedometer
    // configuration succeed.
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

private:
    // Write a single byte to an IMU register over I2C. Returns true on ACK.
    bool writeRegister(uint8_t reg, uint8_t value);

    // Configure the LSM6DSOX embedded pedometer engine (called from begin()).
    bool initHardwarePedometer();

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
};

#endif // NANOWEAR_HARDWARE_IMU_H
