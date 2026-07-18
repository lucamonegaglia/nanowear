/*
 ***************************************************************************
 * lsm6dsox_pedometer — port of STMicroelectronics' LSM6DSOX pedometer
 * example to the Arduino Nano RP2040 Connect.
 *
 * Original: STMems_Standard_C_drivers/lsm6dsox_STdC/examples/lsm6dsox_pedometer.c
 *           (BSD-3-Clause, STMicroelectronics)
 *
 * What changed vs. the original:
 *   - The ST eval-board platform layer (HAL SPI/I2C/UART, board #defines) is
 *     replaced by an Arduino port that talks to the on-board LSM6DSOX over the
 *     default `Wire` bus (the Nano RP2040 Connect routes `Wire` to the internal
 *     IMU, 7-bit address 0x6A).
 *   - `platform_write`/`platform_read` are implemented with `Wire`.
 *   - `tx_com` prints to the USB Serial monitor.
 *   - `platform_delay` maps to Arduino `delay()`.
 *   - `platform_init` starts `Wire` and `Serial`.
 *   - The example's single `lsm6dsox_pedometer()` function is split into
 *     Arduino `setup()` (one-time init) and `loop()` (the polling main loop).
 *     The loop is throttled with `millis()` instead of a busy spin, to match
 *     the project's non-blocking convention.
 *
 * The LSM6DSOX embedded pedometer runs entirely in hardware — no step
 * algorithm on the MCU. Status is read by polling the latched status register
 * (the INT1 line is routed but not used by this polling sketch).
 ***************************************************************************
 */

#include <Arduino.h>
#include <Wire.h>
#include "lsm6dsox_reg.h"

/* Private macro ----------------------------------------------------------*/
#define BOOT_TIME                 10   // ms — sensor boot time before first access

/* 7-bit I2C address of the on-board LSM6DSOX.
 * NOTE: the driver header's LSM6DSOX_I2C_ADD_L (0xD5) is the *8-bit* address
 * (0x6A << 1) that the STM32 HAL expects. Arduino Wire wants the 7-bit form. */
#define LSM6DSOX_I2C_ADDR         0x6A

/* Throttle for the polling loop (ms). Non-blocking: loop() returns to the
 * scheduler between polls instead of spinning the I2C bus. */
#define POLL_INTERVAL_MS          50

/* Private variables ------------------------------------------------------*/
static uint8_t whoamI, rst;
static lsm6dsox_all_sources_t status;
static uint16_t steps;

/* We ignore dev_ctx.handle and talk to the global Wire instance directly,
 * so no bus handle pointer is needed here. */
static stmdev_ctx_t dev_ctx;

/* Private functions (platform layer) -------------------------------------*/
static int32_t platform_write(void *handle, uint8_t reg, const uint8_t *bufp,
                              uint16_t len);
static int32_t platform_read(void *handle, uint8_t reg, uint8_t *bufp,
                             uint16_t len);
static void tx_com(const char *msg);
static void platform_delay(uint32_t ms);
static void platform_init(void);

/* ----------------------------------------------------------------------- */
void setup(void)
{
  /* Initialize mems driver interface */
  dev_ctx.write_reg = platform_write;
  dev_ctx.read_reg  = platform_read;
  dev_ctx.mdelay    = platform_delay;
  dev_ctx.handle    = NULL;

  /* Init test platform (Wire + Serial) */
  platform_init();

  /* Wait sensor boot time */
  platform_delay(BOOT_TIME);

  /* Check device ID — if this hangs/prints nothing, the IMU is not answering
   * on Wire@0x6A. */
  lsm6dsox_device_id_get(&dev_ctx, &whoamI);
  Serial.print("WHO_AM_I: 0x");
  Serial.println(whoamI, HEX);
  if (whoamI != LSM6DSOX_ID) {
    Serial.println("ERROR: LSM6DSOX not found (WHO_AM_I mismatch)");
    while (1) { delay(1000); }
  }

  /* Restore default configuration */
  lsm6dsox_reset_set(&dev_ctx, PROPERTY_ENABLE);
  do {
    lsm6dsox_reset_get(&dev_ctx, &rst);
    delay(1);                 // brief yield while reset completes
  } while (rst);

  /* Disable I3C interface (the Nano RP2040 Connect uses I2C only) */
  lsm6dsox_i3c_disable_set(&dev_ctx, LSM6DSOX_I3C_DISABLE);

  /* Enable Block Data Update (output registers not updated until MSB+LSB read) */
  lsm6dsox_block_data_update_set(&dev_ctx, PROPERTY_ENABLE);

  /* Set full scale: accel 4g, gyro 2000dps */
  lsm6dsox_xl_full_scale_set(&dev_ctx, LSM6DSOX_4g);
  lsm6dsox_gy_full_scale_set(&dev_ctx, LSM6DSOX_2000dps);

  /* Enable pedometer */
  lsm6dsox_pedo_sens_set(&dev_ctx, LSM6DSOX_FALSE_STEP_REJ_ADV_MODE);
  lsm6dsox_emb_sens_t emb_sens;
  emb_sens.step = PROPERTY_ENABLE;
  emb_sens.mlc  = PROPERTY_ENABLE;
  lsm6dsox_embedded_sens_set(&dev_ctx, &emb_sens);

  /* Route step-detector signal onto interrupt pin 1 (read in polling mode) */
  lsm6dsox_pin_int1_route_t pin_int1_route;
  lsm6dsox_pin_int1_route_get(&dev_ctx, &pin_int1_route);
  pin_int1_route.step_detector = PROPERTY_ENABLE;
  lsm6dsox_pin_int1_route_set(&dev_ctx, pin_int1_route);

  /* Configure interrupt pin: base (pulsed) + embedded (latched) notification */
  lsm6dsox_int_notification_set(&dev_ctx, LSM6DSOX_BASE_PULSED_EMB_LATCHED);

  /* Set Output Data Rate. Accel ODR must be >= MLC/pedometer ODR.
   * Gyro off (pedometer only needs the accelerometer). */
  lsm6dsox_xl_data_rate_set(&dev_ctx, LSM6DSOX_XL_ODR_26Hz);
  lsm6dsox_gy_data_rate_set(&dev_ctx, LSM6DSOX_GY_ODR_OFF);

  /* Reset steps counter to 0 */
  lsm6dsox_steps_reset(&dev_ctx);

  Serial.println("Pedometer enabled. Move the board to count steps.");
}

/* ----------------------------------------------------------------------- */
void loop(void)
{
  static uint32_t last_poll = 0;
  uint32_t now = millis();

  if ((now - last_poll) < POLL_INTERVAL_MS) {
    return;                       // non-blocking: let the scheduler run
  }
  last_poll = now;

  /* Read (latched) interrupt source registers in polling mode */
  lsm6dsox_all_sources_get(&dev_ctx, &status);

  if (status.step_detector) {
    lsm6dsox_number_of_steps_get(&dev_ctx, &steps);
    char buf[32];
    snprintf(buf, sizeof(buf), "steps :%u\r\n", (unsigned)steps);
    tx_com(buf);
  }

  /* Heartbeat: print the current step count once per second so the firmware's
   * liveness and I2C link are observable even when the board is stationary
   * (no steps detected). Remove for production. */
  static uint32_t last_beat = 0;
  if ((now - last_beat) >= 1000) {
    last_beat = now;
    lsm6dsox_number_of_steps_get(&dev_ctx, &steps);
    char buf[32];
    snprintf(buf, sizeof(buf), "steps :%u (heartbeat)\r\n", (unsigned)steps);
    tx_com(buf);
  }
}

/* ----------------------------------------------------------------------- */
/* Platform layer: Wire (I2C) on the Nano RP2040 Connect                   */
/* ----------------------------------------------------------------------- */

/*
 * @brief  Write generic device register over I2C (Wire)
 */
static int32_t platform_write(void *handle, uint8_t reg, const uint8_t *bufp,
                              uint16_t len)
{
  (void)handle;
  Wire.beginTransmission(LSM6DSOX_I2C_ADDR);
  Wire.write(reg);
  Wire.write(bufp, len);
  Wire.endTransmission();
  return 0;
}

/*
 * @brief  Read generic device register(s) over I2C (Wire)
 *
 * The LSM6DSOX auto-increments the register address across a multi-byte read,
 * so we write the start register once, then read `len` bytes.
 */
static int32_t platform_read(void *handle, uint8_t reg, uint8_t *bufp,
                             uint16_t len)
{
  (void)handle;
  Wire.beginTransmission(LSM6DSOX_I2C_ADDR);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) {   // false = repeated start
    return -1;
  }
  Wire.requestFrom((uint8_t)LSM6DSOX_I2C_ADDR, (uint8_t)len, (uint8_t)true);
  for (uint16_t i = 0; i < len; i++) {
    bufp[i] = Wire.read();
  }
  return 0;
}

/*
 * @brief  platform specific output to terminal (USB Serial)
 */
static void tx_com(const char *msg)
{
  Serial.print(msg);
}

/*
 * @brief  platform specific delay
 */
static void platform_delay(uint32_t ms)
{
  delay(ms);
}

/*
 * @brief  platform specific initialization (Wire + Serial)
 */
static void platform_init(void)
{
  Wire.begin();
  Wire.setClock(400000);          // 400 kHz I2C
  Serial.begin(115200);
  /* Wait (bounded) for a USB CDC connection so logs are visible, but never
   * hang forever if no monitor is attached — the pedometer still runs. */
  uint32_t t0 = millis();
  while (!Serial && (millis() - t0) < 3000) {
    delay(1);
  }
}
