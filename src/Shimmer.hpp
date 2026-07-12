#pragma once
#include <cmath>
#include <vector>
#include <algorithm>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// ---------------------------------------------------------------------------
// Shimmer Effect: Pitch shifted reverb/delay
// ---------------------------------------------------------------------------
struct Shimmer {
    std::vector<float> buffer;
    int writePos = 0;
    float phase = 0.f;
    float feedback = 0.6f;
    float lastOut = 0.f;
    float currentDelaySamples = 0.f;
    float peakEnv = 0.f;
    float attackState = 0.f;
    
    float sampleRate = 44100.f;

    void setSampleRate(float sr) {
        sampleRate = sr;
        buffer.assign((size_t)(sr * 4.0f), 0.f); 
    }
    
    void reset() {
        std::fill(buffer.begin(), buffer.end(), 0.f);
        writePos = 0;
        phase = 0.f;
        lastOut = 0.f;
        currentDelaySamples = 0.f;
        peakEnv = 0.f;
        attackState = 0.f;
    }

    float process(float in, float shimmerMix, float decay, float tone, float delaySamples, float attackTime) {
        if (buffer.empty()) return in;

        int size = buffer.size();
        
        currentDelaySamples += 0.005f * (delaySamples - currentDelaySamples);
        
        // Swell envelope processing
        float absIn = std::abs(in);
        if (absIn > peakEnv) peakEnv += 0.1f * (absIn - peakEnv);
        else peakEnv += 0.00005f * (absIn - peakEnv);
        
        float attackCoef = (attackTime < 0.001f) ? 1.0f : 1.0f / (attackTime * sampleRate);
        if (peakEnv > attackState) {
            attackState += attackCoef * (peakEnv - attackState);
        } else {
            attackState += 0.001f * (peakEnv - attackState);
        }
        
        float swellMult = 1.0f;
        if (peakEnv > 0.0001f) {
            swellMult = attackState / peakEnv;
        }
        
        float shimmerIn = in * swellMult;
        
        buffer[writePos] = shimmerIn + lastOut * decay;
        
        // grain length = 50ms
        float grainSize = sampleRate * 0.05f;
        
        float rate = 1.0f / grainSize; // phase increment per sample
        phase += rate;
        if (phase >= 1.0f) phase -= 1.0f;
        
        auto getReadPos = [&](float p) {
            float distance = (1.0f - p) * grainSize; // from grainSize down to 0
            float readPos = writePos - currentDelaySamples - distance;
            while (readPos < 0) readPos += size;
            while (readPos >= size) readPos -= size;
            return readPos;
        };
        
        auto readInterp = [&](float readPos) {
            int idx1 = (int)readPos;
            int idx2 = (idx1 + 1) % size;
            float frac = readPos - idx1;
            return buffer[idx1] * (1.f - frac) + buffer[idx2] * frac;
        };

        float phase2 = phase + 0.5f;
        if (phase2 >= 1.0f) phase2 -= 1.0f;

        // raised cosine envelopes for smoother crossfade
        float env1 = 0.5f * (1.f - std::cos(2.f * M_PI * phase));
        float env2 = 0.5f * (1.f - std::cos(2.f * M_PI * phase2));

        float pitchShifted = readInterp(getReadPos(phase)) * env1 + 
                             readInterp(getReadPos(phase2)) * env2;
                             
        // Lowpass filter (Tone control). alpha from 0.05 (dark) to 1.0 (bright)
        float alpha = 0.05f + tone * 0.95f;
        lastOut = lastOut + alpha * (pitchShifted - lastOut);

        // Mix it back with input
        writePos++;
        if (writePos >= size) writePos = 0;

        return in + lastOut * shimmerMix;
    }
};
