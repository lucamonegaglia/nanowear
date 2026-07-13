# Reference datasheets

Hardware reference documents for the NanoWear board (Arduino Nano RP2040
Connect). Kept in-repo so register-level work (the LSM6DSOX pedometer, the
NINA radio, the RP2040) can be verified against the silicon without hunting
the web.

Retrieved 2026-07-13. Note: ST's own host (`www.st.com`) was unreachable from
the build environment at retrieval time (all `/resource/...` URLs 301-redirect
to the blocked `www` host), so the LSM6DSOX datasheet was pulled from a
distributor CDN mirror. Prefer the canonical ST URL when it is reachable.

| File | What it is | Source (canonical / used) |
|------|-----------|---------------------------|
| `LSM6DSOX_datasheet.pdf` | IMU datasheet (always-on 3-axis accel + 3-axis gyro, embedded pedometer / MLC). **Pedometer registers: `PEDO_TH_MIN` (0x10), `PEDO_THS_REG` (0x11), `PEDO_DEB_REG` (0x12), `STEP_COUNTER_L/H` (0x4B/0x4C), `PEDO_CMD_REG` (0x0F), `EMB_FUNC_EN_A` (0x04); embedded-functions bank via `FUNC_CFG_ACCESS` (0x01) = 0x80.** | ST `st.com/resource/en/datasheet/lsm6dsox.pdf` · mirror `cdn.sparkfun.com/assets/6/f/b/6/b/lsm6dsox.pdf` |
| `RP2040_datasheet.pdf` | RP2040 microcontroller (dual Cortex-M0+, PIO, ADC). | `datasheets.raspberrypi.com/rp2040/rp2040-datasheet.pdf` |
| `Arduino_Nano_RP2040_Connect_schematics.pdf` | Nano RP2040 Connect board schematic (SKU ABX00053) — IMU/NINA/RGB-LED/USB wiring. | `content.arduino.cc/assets/ABX00053-schematics.pdf` |
| `NINA-W10_datasheet.pdf` | NINA-W10x module (ESP32-based) — the board's BLE/Wi-Fi radio. | `u-blox.com/sites/default/files/NINA-W10_DataSheet_UBX-17065507.pdf` |
| `NINA-W10_product_summary.pdf` | NINA-W10x one-page summary. | `u-blox.com/sites/default/files/NINA-W10_ProductSummary_UBX-17051775.pdf` |

## Why these matter

- `LSM6DSOX_datasheet.pdf` is the authority for the embedded pedometer register
  map used by `src/hardware_imu.cpp`. When the step count reads 0, verify the
  enable (`EMB_FUNC_EN_A` PEDO_EN), the threshold/debounce config
  (`PEDO_THS_REG` / `PEDO_DEB_REG` — currently **never written** by the
  firmware, which relies on power-on defaults), and the step-counter read
  address/page against this document.
- `NINA-W10_datasheet.pdf` + the schematics cover the BLE path
  (`COM_MODE_BLE`, `WiFiNINA`).
- `RP2040_datasheet.pdf` covers the MCU (dual-core split in the running-
  dynamics build).
