#include "hardware_imu.h"

// ---------------------------------------------------------------------------
// Low-level I2C register helpers (board only)
// ---------------------------------------------------------------------------

// Write a single byte to a low-level IMU register over Wire.
void HardwareIMU::writeRegister(uint8_t reg, uint8_t val) {
    Wire.beginTransmission(LSM6DSOX_I2C_ADDR);
    Wire.write(reg);
    Wire.write(val);
    Wire.endTransmission();
}

// Read a single byte from a low-level IMU register over Wire.
uint8_t HardwareIMU::readRegister(uint8_t reg) {
    Wire.beginTransmission(LSM6DSOX_I2C_ADDR);
    Wire.write(reg);
    Wire.endTransmission();
    Wire.requestFrom(LSM6DSOX_I2C_ADDR, (uint8_t)1);
    return Wire.available() ? Wire.read() : 0;
}

// ---------------------------------------------------------------------------
// Embedded pedometer configuration
// ---------------------------------------------------------------------------

void HardwareIMU::initHardwarePedometer() {
    // 1. Enable access to the Embedded Functions Configuration register bank.
    writeRegister(FUNC_CFG_ACCESS, FUNC_CFG_BANK);

    // 2. Turn on the pedometer algorithm within the embedded processor core
    //    by setting the PEDO_EN bit.
    writeRegister(EMB_FUNC_EN_A, 0x08);

    // 3. Reset the step-count baseline to 0 (PEDO_RST_STEP bit).
    writeRegister(PEDO_CMD_REG, 0x04);

    // 4. Leave the embedded-function bank to return to normal register ops.
    writeRegister(FUNC_CFG_ACCESS, FUNC_CFG_BANK_CLOSE);

    // 5. Route the pedometer step-detection interrupt natively to INT1.
    writeRegister(FUNC_CFG_ACCESS, ADV_INT_BANK);  // advanced interrupt page
    writeRegister(EMB_FUNC_INT1, 0x08);            // route INT1_STEP_DET detector
    writeRegister(FUNC_CFG_ACCESS, FUNC_CFG_BANK_CLOSE);

    Serial.println("LSM6DSOX Embedded Pedometer Engine Configured.");
}

bool HardwareIMU::begin() {
    // IMU.begin() is the high-level Arduino_LSM6DSOX presence check.
    return IMU.begin();
}

// Read the absolute, cumulative step count from the embedded pedometer.
uint16_t HardwareIMU::readStepCount() {
    // Open functional register access to read the computed metrics.
    writeRegister(FUNC_CFG_ACCESS, FUNC_CFG_BANK);

    uint8_t lowByte  = readRegister(STEP_COUNTER_L);
    uint8_t highByte = readRegister(STEP_COUNTER_H);

    // Close functional register access.
    writeRegister(FUNC_CFG_ACCESS, FUNC_CFG_BANK_CLOSE);

    return combineStepBytes(lowByte, highByte);
}
