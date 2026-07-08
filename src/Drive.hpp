#pragma once
#include <cmath>
#include <algorithm>

inline float dsp_clamp(float v, float lo, float hi) {
    return std::max(lo, std::min(v, hi));
}
// ---------------------------------------------------------------------------
// Distortion / Fuzz waveshaper
// ---------------------------------------------------------------------------
static float applyDrive(float x, float drive, int mode) {
    // drive: 0..1 → mapped to usable gain range per mode
    switch (mode) {
        case 0: { // Soft clip (overdrive) — tanh saturation
            float gain = 1.f + drive * 39.f; // 1x .. 40x
            return std::tanh(x * gain) / std::tanh(gain);
        }
        case 1: { // Hard clip (distortion)
            float gain = 1.f + drive * 49.f;
            float s = x * gain;
            return dsp_clamp(s, -1.f, 1.f);
        }
        case 2: { // Asymmetric fuzz — diode-style
            // Positive half: soft clip; negative half: harder clip + extra gain
            float gain = 1.f + drive * 79.f;
            float s = x * gain;
            if (s >= 0.f)
                return std::tanh(s);
            else
                return -std::pow(std::tanh(-s * 1.5f), 0.7f);
        }
        default:
            return x;
    }
}
