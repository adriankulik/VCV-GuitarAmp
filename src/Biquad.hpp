#pragma once
#include <cmath>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// ---------------------------------------------------------------------------
// Biquad filter — direct form II transposed
// Covers low-shelf, high-shelf, peaking, lowpass, highpass
// ---------------------------------------------------------------------------
struct Biquad {
    float b0 = 1.f, b1 = 0.f, b2 = 0.f;
    float a1 = 0.f, a2 = 0.f;
    float z1 = 0.f, z2 = 0.f;

    float process(float x) {
        float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }

    void reset() { z1 = z2 = 0.f; }

    // Low-shelf at frequency fc (Hz), gain in dB, sampleRate in Hz
    void setLowShelf(float fc, float gainDb, float sampleRate) {
        float A  = std::pow(10.f, gainDb / 40.f);
        float w0 = 2.f * M_PI * fc / sampleRate;
        float cosw = std::cos(w0);
        float sinw = std::sin(w0);
        float S  = 1.f; // shelf slope (1 = max slope)
        float alpha = sinw / 2.f * std::sqrt((A + 1.f / A) * (1.f / S - 1.f) + 2.f);

        float a0 =         (A + 1.f) + (A - 1.f) * cosw + 2.f * std::sqrt(A) * alpha;
        b0 = A * ((A + 1.f) - (A - 1.f) * cosw + 2.f * std::sqrt(A) * alpha) / a0;
        b1 = 2.f * A * ((A - 1.f) - (A + 1.f) * cosw) / a0;
        b2 = A * ((A + 1.f) - (A - 1.f) * cosw - 2.f * std::sqrt(A) * alpha) / a0;
        a1 = -2.f * ((A - 1.f) + (A + 1.f) * cosw) / a0;
        a2 = ((A + 1.f) + (A - 1.f) * cosw - 2.f * std::sqrt(A) * alpha) / a0;
    }

    // High-shelf at frequency fc (Hz), gain in dB
    void setHighShelf(float fc, float gainDb, float sampleRate) {
        float A  = std::pow(10.f, gainDb / 40.f);
        float w0 = 2.f * M_PI * fc / sampleRate;
        float cosw = std::cos(w0);
        float sinw = std::sin(w0);
        float S  = 1.f;
        float alpha = sinw / 2.f * std::sqrt((A + 1.f / A) * (1.f / S - 1.f) + 2.f);

        float a0 =        (A + 1.f) - (A - 1.f) * cosw + 2.f * std::sqrt(A) * alpha;
        b0 = A * ((A + 1.f) + (A - 1.f) * cosw + 2.f * std::sqrt(A) * alpha) / a0;
        b1 = -2.f * A * ((A - 1.f) + (A + 1.f) * cosw) / a0;
        b2 = A * ((A + 1.f) + (A - 1.f) * cosw - 2.f * std::sqrt(A) * alpha) / a0;
        a1 = 2.f * ((A - 1.f) - (A + 1.f) * cosw) / a0;
        a2 = ((A + 1.f) - (A - 1.f) * cosw - 2.f * std::sqrt(A) * alpha) / a0;
    }

    // Peaking EQ at fc (Hz), bandwidth Q, gain in dB
    void setPeaking(float fc, float Q, float gainDb, float sampleRate) {
        float A  = std::pow(10.f, gainDb / 40.f);
        float w0 = 2.f * M_PI * fc / sampleRate;
        float alpha = std::sin(w0) / (2.f * Q);

        float a0 = 1.f + alpha / A;
        b0 = (1.f + alpha * A) / a0;
        b1 = (-2.f * std::cos(w0)) / a0;
        b2 = (1.f - alpha * A) / a0;
        a1 = b1;
        a2 = (1.f - alpha / A) / a0;
    }

    // 1-pole highpass for DC blocking / gate detector
    void setHighpass(float fc, float sampleRate) {
        float w0 = 2.f * M_PI * fc / sampleRate;
        float cosw = std::cos(w0);
        float a0 = 1.f + (1.f - cosw) / 2.f;  // approximation; full biquad HP below
        // Proper 2nd-order Butterworth highpass
        float sinw = std::sin(w0);
        float alpha = sinw / (2.f * 0.7071f); // Q = 1/sqrt(2)
        float ia0 = 1.f / (1.f + alpha);
        b0 = ((1.f + cosw) / 2.f) * ia0;
        b1 = -(1.f + cosw) * ia0;
        b2 = b0;
        a1 = (-2.f * cosw) * ia0;
        a2 = (1.f - alpha) * ia0;
        (void)a0; // suppress unused warning
    }
};
