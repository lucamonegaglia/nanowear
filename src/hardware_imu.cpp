#include "hardware_imu.h"

// ---------------------------------------------------------------------------
// Low-level I2C register helpers (board only)
// ---------------------------------------------------------------------------

// Write a single byte to a low-level IMU register over Wire. Returns true when
// the slave ACKs the transfer (Wire.endTransmission() == 0).
bool HardwareIMU::writeRegister(uint8_t reg, uint8_t val) {
    Wire.beginTransmission(LSM6DSOX_I2C_ADDR);
    Wire.write(reg);
    Wire.write(val);
    return Wire.endTransmission() == 0;
}

// Open / close the Embedded Functions Configuration register bank. These are
// the two bank-select writes shared by every register access below.
bool HardwareIMU::openFuncBank() {
    return writeRegister(FUNC_CFG_ACCESS, FUNC_CFG_BANK);
}
bool HardwareIMU::closeFuncBank() {
    return writeRegister(FUNC_CFG_ACCESS, FUNC_CFG_BANK_CLOSE);
}

// ---------------------------------------------------------------------------
// Embedded pedometer configuration
// ---------------------------------------------------------------------------

bool HardwareIMU::initHardwarePedometer() {
    bool ok = true;
    // 1. Enable access to the Embedded Functions Configuration register bank.
    ok &= openFuncBank();

    // 2. Turn on the pedometer algorithm within the embedded processor core
    //    by setting the PEDO_EN bit.
    ok &= writeRegister(EMB_FUNC_EN_A, 0x08);

    // 3. Reset the step-count baseline to 0 (PEDO_RST_STEP bit).
    ok &= writeRegister(PEDO_CMD_REG, 0x04);

    // 4. Leave the embedded-function bank to return to normal register ops.
    ok &= closeFuncBank();

    // 5. Route the pedometer step-detection interrupt natively to INT1.
    ok &= writeRegister(FUNC_CFG_ACCESS, ADV_INT_BANK);  // advanced interrupt page
    ok &= writeRegister(EMB_FUNC_INT1, 0x08);            // route INT1_STEP_DET detector
    ok &= closeFuncBank();

    if (ok) {
        Serial.println("LSM6DSOX Embedded Pedometer Engine Configured.");
    } else {
        Serial.println("Error: LSM6DSOX pedometer configuration failed.");
    }
    return ok;
}

bool HardwareIMU::begin() {
    // IMU.begin() is the high-level Arduino_LSM6DSOX presence check.
    if (!IMU.begin()) return false;
    // Configure the embedded pedometer engine (resets the hardware count to 0).
    return initHardwarePedometer();
}

void HardwareIMU::resetPedometerSteps() {
    // Open the embedded-function config bank, pulse the PEDO_RST_STEP bit, and
    // return to the default register page. Same register dance as init.
    writeRegister(FUNC_CFG_ACCESS, FUNC_CFG_BANK);
    writeRegister(PEDO_CMD_REG, 0x04);
    writeRegister(FUNC_CFG_ACCESS, FUNC_CFG_BANK_CLOSE);
}

// Read the absolute, cumulative step count from the embedded pedometer.
bool HardwareIMU::readStepCount(uint16_t& out) {
    // Open functional register access to read the computed metrics.
    openFuncBank();

    // The two count bytes are adjacent; with the sub-address auto-increment bit
    // set, a single repeated-start burst reads both in one I2C transaction
    // (vs. two separate STOP+START reads).
    Wire.beginTransmission(LSM6DSOX_I2C_ADDR);
    Wire.write(STEP_COUNTER_L | SUBADDR_AUTO_INC);
    Wire.endTransmission(false);   // repeated-start: hold the bus for the read
    uint8_t n = Wire.requestFrom(LSM6DSOX_I2C_ADDR, (uint8_t)2);
    uint8_t lowByte  = (n >= 1) ? static_cast<uint8_t>(Wire.read()) : 0;
    uint8_t highByte = (n == 2) ? static_cast<uint8_t>(Wire.read()) : 0;

    // Close functional register access.
    closeFuncBank();

    if (n != 2) return false;     // partial / failed read: signal the error
    out = combineStepBytes(lowByte, highByte);
    return true;
}
