#include "hardware_imu.h"

// ---------------------------------------------------------------------------
// ST standard-C driver platform layer (Wire / I2C)
// ---------------------------------------------------------------------------
// The vendored lsm6dsox_reg.c driver is transport-agnostic: it calls these
// callbacks, which forward to the on-board Wire bus at the IMU's 7-bit address
// (0x6A). The driver header's LSM6DSOX_I2C_ADD_L (0xD5) is the 8-bit form the
// STM32 HAL expects; Arduino Wire wants the 7-bit form, so we use
// HardwareIMU::LSM6DSOX_I2C_ADDR here. `handle` is unused (single IMU).

static int32_t stm_platform_write(void* handle, uint8_t reg,
                                  const uint8_t* bufp, uint16_t len) {
    (void)handle;
    Wire.beginTransmission(HardwareIMU::LSM6DSOX_I2C_ADDR);
    Wire.write(reg);
    Wire.write(bufp, len);
    Wire.endTransmission();
    return 0;
}

static int32_t stm_platform_read(void* handle, uint8_t reg,
                                 uint8_t* bufp, uint16_t len) {
    (void)handle;
    Wire.beginTransmission(HardwareIMU::LSM6DSOX_I2C_ADDR);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) return -1;   // repeated-start
    Wire.requestFrom((uint8_t)HardwareIMU::LSM6DSOX_I2C_ADDR, (uint8_t)len,
                     (uint8_t)true);
    for (uint16_t i = 0; i < len; i++) {
        bufp[i] = static_cast<uint8_t>(Wire.read());
    }
    return 0;
}

static void stm_platform_delay(uint32_t ms) {
    delay(ms);
}

// ---------------------------------------------------------------------------
// Low-level single-byte I2C helpers (FIFO path only)
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

// ---------------------------------------------------------------------------
// Construction + lifecycle
// ---------------------------------------------------------------------------

HardwareIMU::HardwareIMU() {
    dev_ctx_.write_reg = stm_platform_write;
    dev_ctx_.read_reg  = stm_platform_read;
    dev_ctx_.mdelay    = stm_platform_delay;
    dev_ctx_.handle    = nullptr;
}

bool HardwareIMU::begin() {
    // Configure the embedded pedometer engine + presence check (resets the
    // hardware count to 0). Under running dynamics, also stream the FIFO.
    if (!initHardwarePedometer()) return false;
#ifdef NANOWEAR_RUNNING_DYNAMICS
    return initFifo();
#else
    return true;
#endif
}

// ---------------------------------------------------------------------------
// Embedded pedometer configuration (ST standard-C driver)
// ---------------------------------------------------------------------------

bool HardwareIMU::initHardwarePedometer() {
    uint8_t whoamI = 0;

    // Presence check via WHO_AM_I (replaces the Arduino_LSM6DSOX IMU.begin()).
    lsm6dsox_device_id_get(&dev_ctx_, &whoamI);
    if (whoamI != LSM6DSOX_ID) {
        Serial.println("Error: LSM6DSOX not found (WHO_AM_I mismatch).");
        return false;
    }

    // Restore default configuration, then disable I3C (this board is I2C-only).
    uint8_t rst = 1;
    lsm6dsox_reset_set(&dev_ctx_, PROPERTY_ENABLE);
    do {
        lsm6dsox_reset_get(&dev_ctx_, &rst);
        delay(1);
    } while (rst);

    lsm6dsox_i3c_disable_set(&dev_ctx_, LSM6DSOX_I3C_DISABLE);
    lsm6dsox_block_data_update_set(&dev_ctx_, PROPERTY_ENABLE);

    // Full scale: accel ±4 g, gyro ±2000 dps.
    lsm6dsox_xl_full_scale_set(&dev_ctx_, LSM6DSOX_4g);
    lsm6dsox_gy_full_scale_set(&dev_ctx_, LSM6DSOX_2000dps);

    // Enable the embedded pedometer (advanced false-step-rejection mode) and the
    // embedded-sensor block that hosts it.
    lsm6dsox_pedo_sens_set(&dev_ctx_, LSM6DSOX_FALSE_STEP_REJ_ADV_MODE);
    lsm6dsox_emb_sens_t emb_sens;
    emb_sens.step = PROPERTY_ENABLE;
    emb_sens.mlc  = PROPERTY_ENABLE;
    lsm6dsox_embedded_sens_set(&dev_ctx_, &emb_sens);

    // Route the step detector onto INT1 (polled below; no ISR is used).
    lsm6dsox_pin_int1_route_t pin_int1_route;
    lsm6dsox_pin_int1_route_get(&dev_ctx_, &pin_int1_route);
    pin_int1_route.step_detector = PROPERTY_ENABLE;
    lsm6dsox_pin_int1_route_set(&dev_ctx_, pin_int1_route);

    // INT1 notification: base (pulsed) + embedded (latched).
    lsm6dsox_int_notification_set(&dev_ctx_, LSM6DSOX_BASE_PULSED_EMB_LATCHED);

    // Accel ODR must be >= the pedometer's requirement (26 Hz here); gyro off
    // (the pedometer uses only the accelerometer). Running-dynamics (if built)
    // raises the accel ODR to 1.66 kHz in initFifo(), which still satisfies it.
    lsm6dsox_xl_data_rate_set(&dev_ctx_, LSM6DSOX_XL_ODR_26Hz);
    lsm6dsox_gy_data_rate_set(&dev_ctx_, LSM6DSOX_GY_ODR_OFF);

    // Reset the hardware step counter to 0.
    lsm6dsox_steps_reset(&dev_ctx_);

    Serial.println("LSM6DSOX Embedded Pedometer Engine Configured.");
    return true;
}

// Read the absolute, cumulative step count from the embedded pedometer. The ST
// driver handles the embedded-function bank switch internally. On a transport
// error `out` is left unchanged (per the IMUSensor contract).
bool HardwareIMU::readStepCount(uint16_t& out) {
    uint16_t steps = 0;
    if (lsm6dsox_number_of_steps_get(&dev_ctx_, &steps) != 0) return false;
    out = steps;
    return true;
}

// Zero the embedded pedometer's step counter (PEDO_RST_STEP), leaving the
// algorithm enabled. Used by the debug console and the BLE "reset" command.
bool HardwareIMU::resetStepCount() {
    return lsm6dsox_steps_reset(&dev_ctx_) == 0;
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
