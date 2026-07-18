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
// STM C driver transport seam (Wire-backed)
// ---------------------------------------------------------------------------
// The official lsm6dsox_reg.h driver is a struct of function pointers
// (stmdev_ctx_t); these three static methods implement that seam over the
// Arduino `Wire` bus. `handle` is &Wire (set in begin()). Return convention
// follows the driver: 0 == success, negative == error.

// Write `len` bytes starting at `reg` (single I2C transaction, STOP at end).
int32_t HardwareIMU::platform_write_(void* handle, uint8_t reg,
                                     const uint8_t* bufp, uint16_t len) {
    TwoWire* w = static_cast<TwoWire*>(handle);
    w->beginTransmission(LSM6DSOX_I2C_ADDR);
    w->write(reg);
    w->write(bufp, len);
    return (w->endTransmission() == 0) ? 0 : -1;
}

// Read `len` bytes starting at `reg` using a repeated-start (no STOP between the
// address write and the data read) so the read is atomic and the bus is not
// released mid-transaction.
int32_t HardwareIMU::platform_read_(void* handle, uint8_t reg,
                                    uint8_t* bufp, uint16_t len) {
    TwoWire* w = static_cast<TwoWire*>(handle);
    w->beginTransmission(LSM6DSOX_I2C_ADDR);
    w->write(reg);
    if (w->endTransmission(false) != 0) return -1;   // repeated-start
    if (w->requestFrom(LSM6DSOX_I2C_ADDR, static_cast<uint8_t>(len)) != len)
        return -1;
    for (uint16_t i = 0; i < len; i++)
        bufp[i] = static_cast<uint8_t>(w->read());
    return 0;
}

// Millisecond delay the driver uses for reset / power-up waits.
void HardwareIMU::platform_delay_(uint32_t ms) { delay(ms); }

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

    // Wire up the ST C driver context over I2C. The platform_* helpers above
    // carry the actual bus access; &Wire is the opaque handle the driver passes
    // back. Done unconditionally so the FIFO path is always valid if enabled.
    ctx_.write_reg = platform_write_;
    ctx_.read_reg  = platform_read_;
    ctx_.mdelay    = platform_delay_;
    ctx_.handle    = &Wire;

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

// Configure the LSM6DSOX FIFO to stream accel + gyro at 1.66 kHz in continuous
// (streaming) mode, using the official ST C driver (mirrors lsm6dsox_fifo.c).
// The driver programs the sensor's tag-based FIFO; read() then repacks each
// accel+gyro pair into the flat 12-byte record decodeFifo expects. The scale
// factors and sample period are cached with their existing values so the
// host-side decoder is byte-for-byte unchanged.
bool HardwareIMU::initFifo() {
    bool ok = true;
    int32_t rc;

    // Output registers update only after both MSB and LSB are read (no torn
    // samples across the I2C read).
    rc = lsm6dsox_block_data_update_set(&ctx_, PROPERTY_ENABLE);
    ok &= (rc == 0);

    // Full scale: accel ±4 g, gyro ±2000 dps — must match the decode scales.
    rc = lsm6dsox_xl_full_scale_set(&ctx_, LSM6DSOX_4g);
    ok &= (rc == 0);
    rc = lsm6dsox_gy_full_scale_set(&ctx_, LSM6DSOX_2000dps);
    ok &= (rc == 0);

    // FIFO watermark (in entries) bounds a single drain; read() still drains by
    // level, so this is a safety ceiling rather than a trigger.
    rc = lsm6dsox_fifo_watermark_set(&ctx_, 256);
    ok &= (rc == 0);

    // Batch accel + gyro into the FIFO at 1.66 kHz, no decimation.
    rc = lsm6dsox_fifo_xl_batch_set(&ctx_, LSM6DSOX_XL_BATCHED_AT_1667Hz);
    ok &= (rc == 0);
    rc = lsm6dsox_fifo_gy_batch_set(&ctx_, LSM6DSOX_GY_BATCHED_AT_1667Hz);
    ok &= (rc == 0);

    // Continuous (streaming) FIFO mode.
    rc = lsm6dsox_fifo_mode_set(&ctx_, LSM6DSOX_STREAM_MODE);
    ok &= (rc == 0);

    // Output data rate: accel + gyro at 1.66 kHz (drives the FIFO batches).
    rc = lsm6dsox_xl_data_rate_set(&ctx_, LSM6DSOX_XL_ODR_1667Hz);
    ok &= (rc == 0);
    rc = lsm6dsox_gy_data_rate_set(&ctx_, LSM6DSOX_GY_ODR_1667Hz);
    ok &= (rc == 0);

    // Cache the scale factors + sample period the host decoder needs. Kept
    // identical to the prior values so decodeFifo output is unchanged.
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

// Drain the LSM6DSOX FIFO into `out` (up to `cap` bytes) and repack it into the
// flat 12-byte (accel[6] ‖ gyro[6]) layout decodeFifo expects. The hardware FIFO
// is tag-based — each entry is 1 tag byte + 6 data bytes, with accel/gyro
// entries interleaved — read through the ST driver's tag + raw getters. We stage
// each accel and gyro payload and emit a 12-byte record once a matching pair
// arrives, so the rest of the pipeline (decodeFifo / GaitDetector) is unchanged.
bool HardwareIMU::read(uint8_t* out, size_t cap, size_t& filled) {
    filled = 0;

    // Number of unread FIFO entries (each entry = tag + 6 data bytes).
    uint16_t entries = 0;
    if (lsm6dsox_fifo_data_level_get(&ctx_, &entries) != 0) return false;
    if (entries == 0) return false;

    uint8_t accelBuf[6] = {0};
    uint8_t gyroBuf[6]  = {0};
    bool haveAccel = false, haveGyro = false;

    while (entries--) {
        lsm6dsox_fifo_tag_t tag;
        uint8_t raw[6];
        // Read one entry: its tag, then its 6 data bytes. The driver advances
        // the FIFO read pointer past the whole entry on each call.
        if (lsm6dsox_fifo_sensor_tag_get(&ctx_, &tag) != 0) return false;
        if (lsm6dsox_fifo_out_raw_get(&ctx_, raw)   != 0) return false;

        if (tag == LSM6DSOX_XL_NC_TAG) {
            memcpy(accelBuf, raw, 6); haveAccel = true;
        } else if (tag == LSM6DSOX_GYRO_NC_TAG) {
            memcpy(gyroBuf, raw, 6); haveGyro = true;
        } else {
            continue;   // unhandled tag (e.g. timestamp / step) — skip the entry
        }

        // Got a complete accel+gyro pair: pack it as accel[6] ‖ gyro[6].
        if (haveAccel && haveGyro) {
            if (filled + 12 > cap) break;   // output full; stop draining
            memcpy(out + filled,     accelBuf, 6);
            memcpy(out + filled + 6, gyroBuf,  6);
            filled += 12;
            haveAccel = haveGyro = false;
        }
    }

    // true if we emitted at least one full sample; false on empty/partial drain.
    return filled > 0;
}
