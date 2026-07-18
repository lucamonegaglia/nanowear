# STM LSM6DSOX pedometer example (Nano RP2040 Connect)

A faithful port of STMicroelectronics' `lsm6dsox_pedometer.c` (from
`STMems_Standard_C_drivers/lsm6dsox_STdC/examples`) to the Arduino Nano
RP2040 Connect, using the vendor's **standard-C driver** (`lsm6dsox_reg.c/.h`)
talked to over the on-board `Wire` I2C bus.

The LSM6DSOX embedded pedometer runs entirely in hardware — the MCU only
configures it and reads the step counter. No step algorithm on the RP2040.

## What's in here

- `src/main.cpp` — the example ported to Arduino `setup()`/`loop()`. The
  original's STM32/HAL platform layer (`platform_write`/`platform_read`/
  `tx_com`/`platform_delay`/`platform_init`) is replaced by a `Wire` (I2C)
  implementation at the IMU's 7-bit address `0x6A`, with output on the USB
  Serial monitor.
- `src/lsm6dsox_reg.c` / `src/lsm6dsox_reg.h` — the STM standard-C driver
  (BSD-3-Clause, STMicroelectronics), copied verbatim from the cloned
  `STMems_Standard_C_drivers` repo (its `driver/` is a git submodule).
- `platformio.ini` — `nanorp2040connect` env, `framework = arduino`, no
  external libraries.

## Build / flash / verify

```bash
cd examples/stm-pedometer
pio run -t upload          # build + flash
pio device monitor         # or: python3 scripts/dump_log.py-style reader
```

On boot the sketch prints `WHO_AM_I`, `Pedometer enabled…`, then a 1 Hz
`steps :N (heartbeat)` line. With the board stationary `N` stays `0`;
**shake/move the board** and the hardware pedometer increments `N`.

The 1 Hz heartbeat is a verification aid (see comment in `loop()`); remove it
for production — the original example only prints on step detection.

## Notes / deviations from the stock example

- The driver header's `LSM6DSOX_I2C_ADD_L` (`0xD5`) is the **8-bit** I2C
  address the STM32 HAL expects; Arduino `Wire` wants the **7-bit** `0x6A`.
- `Serial` is not waited on indefinitely: `setup()` waits up to 3 s for a
  monitor, then proceeds so the pedometer still runs headless.
- The main loop is throttled with `millis()` (50 ms) instead of a busy spin.
