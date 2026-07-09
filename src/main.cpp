#include <Arduino.h>
#include <Wire.h>
#include <Arduino_LSM6DSOX.h>

// LSM6DSOX I2C Device Address
#define LSM6DSOX_I2C_ADDR      0x6A

// Embedded Functions Register Map
#define FUNC_CFG_ACCESS        0x01
#define EMB_FUNC_EN_A          0x04
#define EMB_FUNC_EXEC_STATUS   0x07
#define PEDO_CMD_REG           0x83
#define INT1_CTRL              0x0D
#define EMB_FUNC_INT1          0x0A

// Step Register Offsets (Page 0 of Embedded Advanced Registers)
#define STEP_COUNTER_L         0x4B
#define STEP_COUNTER_H         0x4C

// Helper function to write directly to low-level IMU registers
void writeRegister(uint8_t reg, uint8_t val) {
    Wire.beginTransmission(LSM6DSOX_I2C_ADDR);
    Wire.write(reg);
    Wire.write(val);
    Wire.endTransmission();
}

// Helper function to read from low-level IMU registers
uint8_t readRegister(uint8_t reg) {
    Wire.beginTransmission(LSM6DSOX_I2C_ADDR);
    Wire.write(reg);
    Wire.endTransmission();
    Wire.requestFrom(LSM6DSOX_I2C_ADDR, (uint8_t)1);
    return Wire.available() ? Wire.read() : 0;
}

void initHardwarePedometer() {
    // 1. Enable access to Embedded Functions Configuration Register bank
    writeRegister(FUNC_CFG_ACCESS, 0x80); 

    // 2. Turn on the pedometer algorithm within the embedded processor core
    writeRegister(EMB_FUNC_EN_A, 0x08); // Set PEDO_EN bit

    // 3. Reset step count baseline to 0
    writeRegister(PEDO_CMD_REG, 0x04); // Set PEDO_RST_STEP bit

    // 4. Disable access to Embedded Functions to return to normal register operations
    writeRegister(FUNC_CFG_ACCESS, 0x00);

    // 5. Route pedometer step detection event interrupts natively to INT1
    writeRegister(FUNC_CFG_ACCESS, 0x40); // Access advanced interrupts page
    writeRegister(EMB_FUNC_INT1, 0x08);   // Route INT1_STEP_DET detector
    writeRegister(FUNC_CFG_ACCESS, 0x00); // Return to default page

    Serial.println("LSM6DSOX Embedded Pedometer Engine Configured.");
}

uint16_t getHardwareStepCount() {
    uint8_t lowByte, highByte;

    // Open functional register access to read computed metrics
    writeRegister(FUNC_CFG_ACCESS, 0x80);
    
    lowByte  = readRegister(STEP_COUNTER_L);
    highByte = readRegister(STEP_COUNTER_H);
    
    // Close functional register access
    writeRegister(FUNC_CFG_ACCESS, 0x00);

    return (uint16_t)((highByte << 8) | lowByte);
}

void setup() {
    Serial.begin(115200);
    
    // Initialize standard Arduino wire architecture
    Wire.begin();

    // Verify sensor presence via high-level layer
    if (!IMU.begin()) {
        Serial.println("Critical Error: Failed to find LSM6DSOX.");
        while (1);
    }

    // Over-write sensor subsystems to boot the hardware pedometer engine
    initHardwarePedometer();
}

void loop() {
    static unsigned long lastMetricsPoll = 0;
    static uint16_t cumulativeSteps = 0;

    unsigned long currentMillis = millis();

    
    // Poll the isolated hardware counter every 2 seconds (Power Optimised)
    if (currentMillis - lastMetricsPoll >= 2000) {
        lastMetricsPoll = currentMillis;
        
        uint16_t hardwareSteps = getHardwareStepCount();
        Serial.print("[PEDOMETER ACTIVATED] Dynamic hardware Steps: ");
        Serial.println(hardwareSteps);

        if (hardwareSteps != cumulativeSteps) {
            cumulativeSteps = hardwareSteps;
            Serial.print("[PEDOMETER ACTIVATED] Dynamic Total Steps: ");
            Serial.println(cumulativeSteps);
        }
    }
}