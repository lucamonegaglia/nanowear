#ifndef NANOWEAR_IMU_H
#define NANOWEAR_IMU_H

#include <stdint.h>

// ---------------------------------------------------------------------------
// IMU interface
// ---------------------------------------------------------------------------
// An abstract view of the motion sensor, decoupled from the concrete
// LSM6DSOX driver. The firmware talks to the sensor only through this
// interface, which lets the step-counting logic be unit-tested on the host
// (native env) using a fake implementation instead of real I2C hardware.
//
// NOTE: the class is deliberately named IMUSensor (not IMU) to avoid colliding
// with the `IMU` macro that Arduino_LSM6DSOX.h #defines to its global object
// (`#define IMU IMU_LSM6DSOX`). That macro would otherwise rewrite every
// `class IMU` / `IMU&` token in the board build into `IMU_LSM6DSOX`, breaking
// compilation. The name also keeps the test double MockIMU unambiguous.
//
// The interface is deliberately minimal: initialise the sensor and read the
// cumulative step count. The underlying transport can fail (e.g. a noisy I2C
// bus), so readStepCount() reports success through its bool return rather than
// smuggling an error into the count — see pedometer.h, which preserves the
// running total across a failed read. Low-level register access lives only in
// the concrete HardwareIMU, never in this abstraction.
//
// The production implementation (HardwareIMU) lives in hardware_imu.cpp and is
// compiled only for the board. A test double (MockIMU) is provided below so
// native tests stay hardware-free and deterministic.
// ---------------------------------------------------------------------------
class IMUSensor {
public:
    virtual ~IMUSensor() = default;

    // Initialize the sensor (and, on the board, the embedded pedometer engine).
    // Returns true on success.
    virtual bool begin() = 0;

    // Read the absolute, cumulative step count maintained by the embedded
    // pedometer engine. The engine is cumulative and saturates at 65535.
    // Returns false on a transport error; in that case `out` is left unchanged.
    virtual bool readStepCount(uint16_t& out) = 0;
};

// ---------------------------------------------------------------------------
// MockIMU — test double for the host (native) unit tests
// ---------------------------------------------------------------------------
// Records calls and returns scripted values. Tests assign `stepCount` /
// `readStepCountResult` and assert on `beginResult`; no real hardware is
// touched.
// ---------------------------------------------------------------------------
class MockIMU : public IMUSensor {
public:
    bool beginResult = true;
    uint16_t stepCount = 0;
    bool readStepCountResult = true;

    bool begin() override {
        return beginResult;
    }

    bool readStepCount(uint16_t& out) override {
        out = stepCount;
        return readStepCountResult;
    }
};

#endif // NANOWEAR_IMU_H
