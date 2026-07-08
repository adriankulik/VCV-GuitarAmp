#pragma once
#include <cmath>

// ---------------------------------------------------------------------------
// Simple noise gate with attack/release
// ---------------------------------------------------------------------------
struct NoiseGate {
    float envelope = 0.f;
    float gainState = 0.f;

    float process(float x, float threshold, float attackMs, float releaseMs, float sampleRate) {
        float attackCoef  = std::exp(-1.f / (sampleRate * attackMs  * 0.001f));
        float releaseCoef = std::exp(-1.f / (sampleRate * releaseMs * 0.001f));

        float level = std::abs(x);
        if (level > envelope)
            envelope = attackCoef  * envelope + (1.f - attackCoef)  * level;
        else
            envelope = releaseCoef * envelope + (1.f - releaseCoef) * level;

        float targetGain = (envelope > threshold) ? 1.f : 0.f;
        gainState = gainState + 0.001f * (targetGain - gainState); // smooth gain transitions
        return x * gainState;
    }

    void reset() { envelope = gainState = 0.f; }
};
