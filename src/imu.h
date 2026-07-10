#ifndef NANOWEAR_IMU_H
#define NANOWEAR_IMU_H

#include <stdint.h>

// ---------------------------------------------------------------------------
// IStepSensor interface
// ---------------------------------------------------------------------------
// An abstract view of the motion sensor, decoupled from the concrete
// LSM6DSOX driver. The firmware talks to the sensor only through this
// interface, which lets the step-counting logic be unit-tested on the host
// (native env) using a fake implementation instead of real I2C hardware.
//
// NOTE: this interface is deliberately NOT named `IMU` — the Arduino
// LSM6DSOX library exposes a global `IMU` object, so reusing that
// identifier would collide with the library's symbol.
//
// The production implementation (HardwareIMU) lives in hardware_imu.cpp and is
// compiled only for the board. A test double (MockStepSensor) is provided
// below so native tests stay hardware-free and deterministic.
// ---------------------------------------------------------------------------
class IStepSensor {
public:
    virtual ~IStepSensor() = default;

    // Initialize the sensor. Returns true on success.
    virtual bool begin() = 0;

    // Write a single byte to an IMU register over I2C.
    virtual void writeRegister(uint8_t reg, uint8_t value) = 0;

    // Read a single byte from an IMU register.
    virtual uint8_t readRegister(uint8_t reg) = 0;

    // Read the absolute step count maintained by the embedded pedometer
    // engine. The engine is cumulative and saturates at 65535.
    virtual uint16_t readStepCount() = 0;
};

// ---------------------------------------------------------------------------
// MockStepSensor — test double for the host (native) unit tests
// ---------------------------------------------------------------------------
// Records calls and returns scripted values. Tests assign `stepCount` and
// assert on the call counters; no real hardware is touched.
// ---------------------------------------------------------------------------
class MockStepSensor : public IStepSensor {
public:
    bool beginCalled = false;
    bool beginResult = true;
    uint16_t stepCount = 0;
    int beginCallCount = 0;
    int readStepCountCallCount = 0;

    bool begin() override {
        beginCalled = true;
        beginCallCount++;
        return beginResult;
    }

    void writeRegister(uint8_t /*reg*/, uint8_t /*value*/) override {
        // No side effects needed for the current logic tests.
    }

    uint8_t readRegister(uint8_t /*reg*/) override {
        return 0;
    }

    uint16_t readStepCount() override {
        readStepCountCallCount++;
        return stepCount;
    }
};

#endif // NANOWEAR_IMU_H
