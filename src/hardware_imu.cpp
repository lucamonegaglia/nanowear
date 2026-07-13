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

// Read a single byte from a low-level IMU register over Wire. Returns 0 when
// the slave NACKs (defensive; the caller treats 0 as "no data").
uint8_t HardwareIMU::readRegister(uint8_t reg) {
    Wire.beginTransmission(LSM6DSOX_I2C_ADDR);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) return 0;   // repeated-start
    uint8_t n = Wire.requestFrom(LSM6DSOX_I2C_ADDR, (uint8_t)1);
    return (n == 1) ? static_cast<uint8_t>(Wire.read()) : 0;
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

    // Read back PEDO_EN to confirm the enable write actually landed on the
    // part (walk-free proof the embedded pedometer engine is on). Surfaced
    // over serial so a missing enable is caught without a walk.
    debugProbe();

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

void HardwareIMU::debugProbe() {
    // Read back the pedometer-enable register (EMB_FUNC_EN_A) in the
    // embedded-functions page and confirm the PEDO_EN bit (0x08) is set.
    openFuncBank();
    uint8_t en = readRegister(EMB_FUNC_EN_A);
    closeFuncBank();
    pedoEnabled_ = (en & 0x08) != 0;
    Serial.print("[PEDOMETER] PEDO_EN: ");
    Serial.println(pedoEnabled_ ? "ON" : "OFF");
}

bool HardwareIMU::readAcceleration(float& x, float& y, float& z) {
    // The Arduino_LSM6DSOX library owns the OUT_X/Y/Z_A register reads and the
    // full-scale scaling (set in IMU.begin()), so reuse it to stay consistent
    // with the sensor configuration. Returns values in g.
    return IMU.readAcceleration(x, y, z);
}

bool HardwareIMU::readGyroscope(float& x, float& y, float& z) {
    // Same as readAcceleration but for the gyroscope; returns values in deg/s.
    return IMU.readGyroscope(x, y, z);
}

bool HardwareIMU::begin() {
    // IMU.begin() is the high-level Arduino_LSM6DSOX presence check.
    if (!IMU.begin()) return false;
    // Configure the embedded pedometer engine (resets the hardware count to 0).
    bool ok = initHardwarePedometer();
    // ALSO stream the raw 6-axis FIFO for running-dynamics detection — but only
    // when that feature is compiled in, so the default (single-core) build
    // behaves exactly like the validated step-counting firmware (no ODR change
    // to the pedometer feed).
#ifdef NANOWEAR_RUNNING_DYNAMICS
    ok &= initFifo();
#endif
    return ok;
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

// Zero the embedded pedometer's step counter (PEDO_RST_STEP bit in PEDO_CMD_REG).
// This only clears the count — the pedometer algorithm (PEDO_EN) stays enabled,
// unlike initHardwarePedometer() which also configures and routes the interrupt.
bool HardwareIMU::resetStepCount() {
    bool ok = true;
    ok &= openFuncBank();
    ok &= writeRegister(PEDO_CMD_REG, 0x04); // PEDO_RST_STEP: reset count to 0
    ok &= closeFuncBank();
    return ok;
}

// ---------------------------------------------------------------------------
// FIFO streaming (running-dynamics source)
// ---------------------------------------------------------------------------

// Configure the LSM6DSOX FIFO to stream accel + gyro at 1.66 kHz in
// continuous (streaming) mode. We deliberately use the ODR-derived timestamp
// in the decoder (not the sensor's own FIFO timestamp) to keep the byte
// format simple and the host parser trivial.
bool HardwareIMU::initFifo() {
    bool ok = true;

    // Accel: ODR 1.66 kHz (1000b), full-scale ±4 g (01b) -> 0x84.
    ok &= writeRegister(CTRL1_XL, 0x84);
    // Gyro:  ODR 1.66 kHz (1000b), full-scale ±2000 dps (11b) -> 0x8C.
    ok &= writeRegister(CTRL2_G,  0x8C);

    // Route accel + gyro into the FIFO with NO decimation (every sample).
    //   DEC_FIFO_XL[2:0] = 001, DEC_FIFO_GY[5:3] = 001.
    ok &= writeRegister(FIFO_CTRL3, 0x09);

    // Continuous (streaming) FIFO mode (MODE[2:0] = 110).
    ok &= writeRegister(FIFO_CTRL5, 0x06);

    // Cache the scale factors + sample period the host decoder needs. These
    // match the full-scale chosen above.
    aScale_ = 0.000122f;   // ±4g  -> 0.122 mg/LSB
    gScale_ = 0.070f;      // ±2000 dps -> 70 mdps/LSB
    dtMs_   = 1000.f / 1660.f;  // sample period at 1.66 kHz
    fifoPattern_ = FifoPattern{};  // accel + gyro, no timestamp (12 B/sample)

    if (ok) {
        Serial.println("LSM6DSOX FIFO configured (accel+gyro @ 1.66 kHz).");
    } else {
        Serial.println("Error: LSM6DSOX FIFO configuration failed.");
    }
    return ok;
}

// Drain the FIFO into `out` (up to `cap` bytes). The sensor reports the
// number of UNREAD 16-bit WORDS in FIFO_STATUS1; our pattern is 6 words
// (12 bytes) per sample, so we burst-read a whole number of samples.
bool HardwareIMU::read(uint8_t* out, size_t cap, size_t& filled) {
    filled = 0;

    // DIFF_FIFO[5:0] = unread 16-bit words.
    uint8_t diff = readRegister(FIFO_STATUS1) & 0x3F;
    if (diff == 0) return false;   // empty: not an error

    const uint8_t bps = fifoPattern_.bytesPerSample();   // 12
    const uint8_t wordsPerSample = bps / 2;                     // 6
    uint8_t nSamples = diff / wordsPerSample;                  // whole samples
    if (nSamples == 0) return false;

    size_t bytes = static_cast<size_t>(nSamples) * bps;
    if (bytes > cap) {                       // trim to the buffer, whole samples
        bytes = (cap / bps) * bps;
        nSamples = static_cast<uint8_t>(bytes / bps);
    }
    if (nSamples == 0) return false;

    // Burst-read FIFO_DATA_OUT_L (3Eh); the FIFO auto-increments on each
    // read, so one repeated-start transaction pulls the whole batch.
    Wire.beginTransmission(LSM6DSOX_I2C_ADDR);
    Wire.write(FIFO_DATA_OUT_L);
    if (Wire.endTransmission(false) != 0) return false;   // NACK -> error
    uint8_t n = Wire.requestFrom(LSM6DSOX_I2C_ADDR, static_cast<uint8_t>(bytes));
    if (n != bytes) return false;

    for (size_t i = 0; i < bytes; i++) {
        out[i] = static_cast<uint8_t>(Wire.read());
    }
    filled = bytes;
    return true;
}
