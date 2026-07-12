#include "gait_detector.h"
#include <cmath>

// ---------------------------------------------------------------------------
// GaitDetector — see gait_detector.h for the metric breakdown and the
// mounting-agnostic / single-ankle-IMU derivability notes.
//
// Detection core (per the Frontiers 2018 foot-IMU study): initial contact
// (IC / foot-strike) and terminal contact (TC / toe-off) are the TWO minima
// of pitch angular velocity within a stride. We alternate them (odd=IC,
// even=TC), derive contact = IC->TC, swing = TC->next IC, step = IC->IC,
// and classify the strike from the foot pitch angle captured at IC.
// ---------------------------------------------------------------------------

namespace {
    // Butterworth tuning. fc is deliberately low: we only care about the
    // gait-band pitch rate, not the sharp impact transient (which we capture
    // separately via the horizontal-accel braking spike).
    constexpr float kFcHz = 30.f;

    // Strike classification: |foot pitch at IC| above this (deg) means a
    // clear rear- vs fore-foot strike; below it is midfoot. Sign is flipped
    // by kPitchSign per physical mounting.
    constexpr float kStrikeThreshDeg = 12.f;

    // Mounting sign for the pitch axis (toe-up = +). Flip to -1 if a given
    // pod orientation reports heel strikes as negative.
    constexpr float kPitchSign = 1.f;

    // Early-stance window (ms) over which we capture the braking/impact spike.
    constexpr float kBrakeWindowMs = 100.f;
}

// --- Biquad (Direct Form I, normalised) --------------------------------
float GaitDetector::Biquad::process(float x) {
    float y = b0 * x + b1 * z1 + b2 * z2 - a1 * y1 - a2 * y2;
    z2 = z1; z1 = x;   // input history
    y2 = y1; y1 = y;   // output history
    return y;
}

void GaitDetector::configureFilters(float fs) {
    // 2nd-order Butterworth low-pass coefficients (Q = 1/sqrt(2)).
    const float w0 = 2.f * 3.14159265f * kFcHz / fs;
    const float cw = cosf(w0);
    const float sw = sinf(w0);
    const float alpha = sw / (2.f * 0.70710678f);   // 1/sqrt(2)
    const float a0 = 1.f + alpha;
    const float b0 = (1.f - cw) / 2.f;
    const float b1 = 1.f - cw;
    const float b2 = (1.f - cw) / 2.f;
    const float a1 = -2.f * cw;
    const float a2 = 1.f - alpha;
    // normalise by a0 so the stored coeffs are the final ones
    auto norm = [&](float c) { return c / a0; };
    lpPitch.b0 = norm(b0);   lpPitch.b1 = norm(b1);   lpPitch.b2 = norm(b2);
    lpPitch.a1 = norm(a1);   lpPitch.a2 = norm(a2);
}

// --- Calibration -------------------------------------------------------
void GaitDetector::calibrate(const ImuSample& s) {
    calAx += s.ax; calAy += s.ay; calAz += s.az;
    calGx += s.gx; calGy += s.gy; calGz += s.gz;
    calGx2 += s.gx * s.gx; calGy2 += s.gy * s.gy; calGz2 += s.gz * s.gz;

    if (calCount == kCalSamples - 1) {
        const float n = static_cast<float>(kCalSamples);
        // Gravity direction in sensor frame (unit) from the mean accel during
        // the stillness window. (Pitch axis is chosen later, from motion.)
        const float gx = calAx / n, gy = calAy / n, gz = calAz / n;
        const float gn = sqrtf(gx * gx + gy * gy + gz * gz);
        if (gn > 1e-6f) {
            gWorldUp[0] = gx / gn; gWorldUp[1] = gy / gn; gWorldUp[2] = gz / gn;
        }
    }
}

// --- Per-stride reset ------------------------------------------------
void GaitDetector::resetStrideWindow(uint32_t ts) {
    strideActive = true;
    posZmin = posZmax = posZ;   // detrend: rebaseline the oscillation window
    braking = false;
    brakeAccum = 0;
    swingPeakRate = 0;
}

// --- Main sample entry ----------------------------------------------
bool GaitDetector::process(const ImuSample& s) {
    if (!calibrated) {
        calibrate(s);
        calCount++;
        if (calCount >= kCalSamples) {
            calibrated = true;
            axisG[0] = axisG[1] = axisG[2] = 0;   // start motion-window accum
            axisG2[0] = axisG2[1] = axisG2[2] = 0;
            axisN = 0;
        }
        lastTs = s.ts;
        return false;   // no stride output during the stillness window
    }

    // Pick the dominant-rotation (pitch) axis from the first kAxisSamples of
    // real motion, so mounting-agnostic selection tracks gait, not stillness.
    if (!axisChosen) {
        axisG[0]  += s.gx; axisG[1]  += s.gy; axisG[2]  += s.gz;
        axisG2[0] += s.gx * s.gx; axisG2[1] += s.gy * s.gy; axisG2[2] += s.gz * s.gz;
        axisN++;
        if (axisN >= kAxisSamples) {
            const float n = static_cast<float>(kAxisSamples);
            const float vx = axisG2[0] / n - (axisG[0] / n) * (axisG[0] / n);
            const float vy = axisG2[1] / n - (axisG[1] / n) * (axisG[1] / n);
            const float vz = axisG2[2] / n - (axisG[2] / n) * (axisG[2] / n);
            if (vx >= vy && vx >= vz)      pitchAxis = 0;
            else if (vy >= vx && vy >= vz) pitchAxis = 1;
            else                              pitchAxis = 2;
            axisChosen = true;
        }
        lastTs = s.ts;
        return false;   // still settling the pitch axis
    }

    // dt from sample timestamps (fall back to ODR if absent/invalid).
    float dtMs = (lastTs != 0 && s.ts > lastTs) ? static_cast<float>(s.ts - lastTs)
                                                     : (1000.f / sampleHz);
    const float dtS = dtMs / 1000.f;
    lastTs = s.ts;

    // Pitch rate (selected axis) -> low-pass.
    const float* g = &s.gx;
    const float pitchRate = lpPitch.process(g[pitchAxis]);

    // Pitch angle (integrated, mounting-signed) — anchored enough for a
    // per-stride strike classification; slow drift is harmless here.
    pitchAngle += kPitchSign * pitchRate * dtS;

    // Vertical accel: project onto world-up, remove gravity, double-integrate
    // for centre-of-mass height. Detrended per stride via posZmin/posZmax.
    const float aUp = s.ax * gWorldUp[0] + s.ay * gWorldUp[1] + s.az * gWorldUp[2];
    const float vertG = aUp - 1.0f;                 // g (gravity removed)
    veloZ += gToMs2(vertG) * dtS;                 // m/s
    posZ  += veloZ * dtS;                          // m
    if (!strideActive) resetStrideWindow(s.ts);
    if (posZ < posZmin) posZmin = posZ;
    if (posZ > posZmax) posZmax = posZ;

    // Horizontal accel magnitude (braking / impact proxy; direction-agnostic
    // because we have no magnetometer heading).
    const float hx = s.ax - aUp * gWorldUp[0];
    const float hy = s.ay - aUp * gWorldUp[1];
    const float hz = s.az - aUp * gWorldUp[2];
    const float hMag = sqrtf(hx * hx + hy * hy + hz * hz);

    if (braking && static_cast<float>(s.ts) <= brakeWindowEnd) {
        if (hMag > brakeAccum) brakeAccum = hMag;
    }

    // Foot-recovery proxy: peak |pitch rate| during swing.
    if (phase == Phase::SWING) {
        const float ar = fabsf(pitchRate);
        if (ar > swingPeakRate) swingPeakRate = ar;
    }

    // --- pitch-rate local-minimum detection (3-sample window) ---
    rPitch[2] = rPitch[1]; rPitch[1] = rPitch[0]; rPitch[0] = pitchRate;
    rTs[2]    = rTs[1];    rTs[1]    = rTs[0];    rTs[0]    = s.ts;
    rPitchAng[2] = rPitchAng[1]; rPitchAng[1] = rPitchAng[0]; rPitchAng[0] = pitchAngle;
    rH[2] = rH[1]; rH[1] = rH[0]; rH[0] = hMag;
    nSeen++;

    if (nSeen >= 3 &&
        rPitch[1] < rPitch[2] && rPitch[1] <= rPitch[0]) {
        // Minimum is the MIDDLE sample (index 1).
        if (handlePitchMin(rTs[1], rPitch[1], rPitchAng[1], rH[1])) {
            return true;
        }
    }
    return false;
}

// --- Minimum -> phase machine ---------------------------------------
bool GaitDetector::handlePitchMin(uint32_t ts, float /*pr*/, float pitchNow,
                                 float hMag) {
    // De-bounce: ignore minima closer than kMinMinGapMs (sensor glitch).
    if (haveMin && static_cast<float>(ts - lastMinTs) < kMinMinGapMs) {
        return false;
    }
    lastMinTs = ts;
    haveMin = true;

    minCount++;
    const bool isIc = (minCount % 2 == 1);   // 1st=IC, 2nd=TC, ...

    if (isIc) {
        icTs = ts;
        pitchAtIc = pitchNow;
        haveIc = true;

        // A stride is complete once we have a prior IC and the TC between.
        if (haveIc && haveTc) {
            const float step    = static_cast<float>(icTs - prevIcTs);
            const float contact = static_cast<float>(tcTs - prevIcTs);
            const float swing   = static_cast<float>(icTs - tcTs);

            if (step >= kMinStepMs && step <= kMaxStepMs &&
                contact >= kMinContactMs && contact <= kMaxContactMs) {
                m.timestampMs        = ts;
                m.contactTimeMs      = contact;
                m.swingTimeMs        = swing;
                m.stepTimeMs         = step;
                m.airTimeMs          = (step > contact) ? (step - contact) : 0.f;
                m.cadenceSpm         = 60000.f / step;
                m.verticalOscillationMm = (posZmax - posZmin) * 1000.f;
                // pitchAtIc already carries the mounting sign (integrated with
                // kPitchSign), so classify on it directly — no second flip.
                if (pitchAtIc > kStrikeThreshDeg)       m.strike = StrikePattern::REARFOOT;
                else if (pitchAtIc < -kStrikeThreshDeg) m.strike = StrikePattern::FOREFOOT;
                else                                      m.strike = StrikePattern::MIDFOOT;
                m.brakingIndex       = brakeAccum;     // in g; proxy for overstride
                m.footRecoveryProxy = swingPeakRate;  // deg/s
                m.valid = true;
                newMetrics = true;

                resetStrideWindow(ts);
                // Re-open the braking-capture window for THIS stance AFTER
                // reading the completed stride's peak, so it isn't zeroed
                // before it is used.
                braking = true;
                brakeAccum = 0;
                brakeWindowEnd = ts + static_cast<uint32_t>(kBrakeWindowMs);
                prevIcTs = icTs;
                return true;
            }
        }
        // No completed stride: open the braking window for this stance and
        // advance the IC reference for the next stride.
        braking = true;
        brakeAccum = 0;
        brakeWindowEnd = ts + static_cast<uint32_t>(kBrakeWindowMs);
        prevIcTs = icTs;       // becomes the "previous IC" for the next stride
        haveIc = true;
        swingPeakRate = 0;
        phase = Phase::STANCE;
    } else {
        tcTs = ts;
        haveTc = true;
        swingPeakRate = 0;   // start of swing: begin tracking recovery
        phase = Phase::SWING;
    }
    return false;
}
