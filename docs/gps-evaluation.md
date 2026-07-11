# GPS Evaluation — Boards & Step/Cadence Fusion

> Companion to `ROADMAP.md` §2 (H2). Summarizes the GPS module options that fit the
> NanoWear ankle tracker and how a GPS signal can improve step counting and cadence
> estimation. Project context: Arduino Nano RP2040 Connect, LSM6DSOX **hardware**
> pedometer (step count + INT1 step-detection interrupt), `TinyGPSPlus` already in
> `platformio.ini`, ankle-worn, ~1000 mAh / 80–90 mA budget.

## TL;DR

- **GPS is a calibrator and validator, not a step counter.** The IMU is the real-time
  step/cadence engine; GPS supplies ground-truth distance and speed. The ankle placement
  is a poor spot for a GPS antenna, so antenna design (external, sky-facing) and
  power-gating are the main challenges for the wearable's own onboard GPS.
- **Onboard module, if going standalone:** prefer **u-blox MAX-M10S** (best sensitivity
  for the bad-sky ankle, lowest power in Super-E mode); **SAM-M8Q** is a close runner-up.
  Avoid the multi-band NEO-M9N (wasted without a good antenna, too power-hungry).
- **Highest-value use of GPS:** stride-length calibration + stationary gating of the IMU
  step count — both work at **1 Hz**, no high-rate fixes needed.

---

## 1. Board evaluation

Hard constraints: 3.3 V logic; small/light for an ankle strap; low power vs the
1000 mAh / 80–90 mA budget; `TinyGPSPlus` parses NMEA from any `Stream`. `Wire` (I2C)
is already used by the LSM6DSOX at `0x6A`, so an I2C GPS needs a distinct address
(PA1010D = `0x10`, fine) or a HardwareSerial UART.

| Module | I/F | Typ. tracking draw | Sensitivity | Rate | Fit for this project |
|---|---|---|---|---|---|
| **u-blox MAX-M10S** | UART or I2C (DDC) | ~25 mA; Super-E mode ~10 mA | Best (−167 dBm) | 1–10 Hz | **Best onboard choice.** Built for battery wearables; best lock in bad-sky ankle condition. |
| **u-blox SAM-M8Q** | UART or I2C | ~23–45 mA | Excellent (−165 dBm) | 1–10 Hz | Strong runner-up; pick if sourcing/BOM favors it. Slightly more power, no real accuracy win. |
| **PA1010D / Adafruit MiniGPS** | I2C (`0x10`) | ~20–25 mA | Moderate (−148 dBm) | 1–10 Hz | Cheapest/smallest; I2C keeps wiring trivial. Weak in ankle's poor sky view — expect dropouts. |
| **Quectel L76K** | I2C or UART | ~20–30 mA | Good | 1–10 Hz | Low-cost alternative; decent sensitivity, common on maker breakouts. |
| **MT3339 (Adafruit Ultimate GPS)** | UART | ~20–40 mA | Good | 1–10 Hz | Reliable, mature, but UART-only and comparatively large. |
| **u-blox NEO-M9N** | UART | ~45–65 mA | Multi-band (L1+L2/L5) | 1–25 Hz | **Overkill.** Multi-band meaningless through an ankle patch; power too high. |

### The placement problem
None of these perform well at the ankle: a shoe/foot blocks the sky, the unit sits near
ground and close to the body, and the strap's sky view exists only mid-swing. Because the
wearable carries its **own** external GPS (per the roadmap, the phone is interface-only,
never a sensor), antenna placement is the crux — an onboard module needs an external
ceramic/patch antenna with a ground plane, oriented sky-facing, ideally on a short pigtail
routed up the leg.

---

## 2. Using GPS for better steps / cadence

Set expectations first:

- The IMU is the **primary** source. The LSM6DSOX pedometer counts steps; its **INT1
  step-detection interrupt timestamps each step**, so instantaneous cadence =
  `60000 / (Δt_ms between steps)` — well above GPS's 1–10 Hz ceiling (a 180 spm runner =
  3 steps/s, faster than 1 Hz GPS can ever resolve). **GPS will not count steps reliably.**
- GPS contributes **distance and speed**, which the step counter lacks. The current
  `Pedometer` is a pure step accumulator with no stride/distance model yet — exactly the
  gap GPS fills.

Techniques, roughly in order of value:

1. **Stride-length calibration (highest value).** Distance = `steps × stride`, but stride
   is currently a blind assumption. Over a GPS segment with good HDOP:
   `stride_m = gpsDistance_m / imuStepCount`. Do this per speed-band (walk vs run) and per
   wearer to build a personalized stride table. Feed stride back so IMU-derived distance
   stays accurate during GPS outages. The LSM6DSOX pedometer can also estimate distance
   internally from a loaded stride parameter — GPS keeps that parameter personal and current.
   Needs only **1 Hz** fixes.
2. **Stationary gating / false-step rejection.** `gpsSpeed ≈ 0` → not moving → any IMU
   step deltas are false positives (fidgeting, vehicle vibration). Gate the `Pedometer`
   delta to zero when `gpsSpeed < ~0.3 m/s`. Directly improves step accuracy.
3. **Cadence cross-validation.** `cadenceFromGps_spm = (gpsSpeed_m_s × 60) / stride_m`.
   Compare to IMU cadence over a rolling window; flag divergence (IMU double-count or GPS
   glitch). Use IMU for live cadence, GPS as the trusted reference.
4. **Pace-adaptive pedometer tuning.** Use GPS speed to pick the gait band (walk/jog/run)
   and retune the LSM6DSOX cadence windows/thresholds (`PEDO_SC_DELTAT`, `PEDO_TOL`,
   min/max step rate in the advanced bank) so detection stays sharp across paces.
5. **Sensor fusion (dead reckoning).** Treat GPS fixes as absolute position corrections;
   blend `distanceFromSteps = Σ steps × stride` with `distanceFromGps` via a simple
   complementary/Kalman filter. Gives continuous distance through outages and refines
   stride online. Production version of 1–3.
6. **Activity classification.** GPS speed profile (mean/variance/acceleration) labels
   walk/run/sprint, selecting the right stride model and pedometer tuning automatically.

**Don't bother:** cadence from position autocorrelation, or RTK/multi-band — the ankle
patch antenna's noise dwarfs sub-meter effects.

---

## 3. Suggested architecture

- IMU stays the real-time engine; GPS runs **only in `LOGGING`**, at **1 Hz** (or 1 fix /
  5–10 s). You need distance integration, not per-step timing.
- Power-gate the module with a P-MOSFET on VCC from a free RP2040 GPIO; cold-start is the
  expensive part, so keep it warm (or in Super-E) only while actively logging.
- Keep `Pedometer` exactly as-is (pure accumulator). Add a thin **stride estimator** that
  consumes `(gpsDistance, imuDeltaSteps)` pairs during good-HDOP windows and exposes
  `strideFor(speedBand)` for distance and cadence-from-GPS.
- Live cadence for display/BLE RSC comes from IMU step-interval timing; GPS cadence is
  computed per-window as the validation reference.

---

## 4. Bottom line

- **Onboard GPS (roadmap direction):** the wearable carries its own external GNSS module
  (phone is interface-only), so board choice, a sky-facing antenna, and power-gating are
  the design levers. **MAX-M10S** is the recommended module.
- **Standalone onboard:** add **MAX-M10S** (or SAM-M8Q), sky-facing external antenna,
  power-gated, 1 Hz while logging.
- **For cadence quality:** GPS calibrates and validates, not counts. The biggest, cheapest
  win is **GPS-driven stride calibration + stationary gating** of the IMU step count.
