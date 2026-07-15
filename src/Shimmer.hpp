#pragma once
#include <cmath>
#include <vector>
#include <algorithm>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// ---------------------------------------------------------------------------
// 1-Pole Filters for Bandpassing & Damping
// ---------------------------------------------------------------------------
struct OnePoleLPF {
    float z1 = 0.f;
    float alpha = 1.f;
    void setCutoff(float freq, float sr) {
        alpha = 1.0f - std::exp(-2.0f * M_PI * freq / sr);
    }
    float process(float in) {
        z1 += alpha * (in - z1);
        return z1;
    }
};

struct OnePoleHPF {
    float z1 = 0.f;
    float alpha = 1.f;
    void setCutoff(float freq, float sr) {
        alpha = 1.0f - std::exp(-2.0f * M_PI * freq / sr);
    }
    float process(float in) {
        z1 += alpha * (in - z1);
        return in - z1;
    }
};

// ---------------------------------------------------------------------------
// Allpass Diffuser for Input Smearing
// ---------------------------------------------------------------------------
struct Allpass {
    std::vector<float> buffer;
    int writePos = 0;
    float lengthSamples = 0;
    float gain = 0.5f;

    void init(float lengthMs, float g, float sr) {
        lengthSamples = lengthMs * 0.001f * sr;
        buffer.assign((size_t)(lengthSamples + 2), 0.f);
        gain = g;
        writePos = 0;
    }
    
    float process(float in) {
        if (buffer.empty()) return in;
        int readPos = writePos - (int)lengthSamples;
        if (readPos < 0) readPos += buffer.size();
        
        float delayed = buffer[readPos];
        float out = -gain * in + delayed;
        buffer[writePos] = in + gain * delayed;
        
        writePos++;
        if (writePos >= (int)buffer.size()) writePos = 0;
        return out;
    }
};

// ---------------------------------------------------------------------------
// Modulated Delay Line for FDN Core
// ---------------------------------------------------------------------------
struct ModDelayLine {
    std::vector<float> buffer;
    int writePos = 0;
    float lengthSamples = 0.f;
    float lfoPhase = 0.f;
    float lfoFreq = 1.f;
    float lfoDepth = 1.f;

    void init(float lengthMs, float lfoF, float lfoD, float sr) {
        lengthSamples = lengthMs * 0.001f * sr;
        buffer.assign((size_t)(lengthSamples + sr * 0.1f), 0.f); // Extra room for modulation
        lfoFreq = lfoF / sr;
        lfoDepth = lfoD;
        writePos = 0;
        lfoPhase = 0.f;
    }

    void write(float in) {
        if (buffer.empty()) return;
        buffer[writePos] = in;
        writePos++;
        if (writePos >= (int)buffer.size()) writePos = 0;
    }

    float read() {
        if (buffer.empty()) return 0.f;
        
        lfoPhase += lfoFreq;
        if (lfoPhase >= 1.f) lfoPhase -= 1.f;
        
        float mod = std::sin(2.f * M_PI * lfoPhase) * lfoDepth;
        float readPos = (float)writePos - lengthSamples - mod;
        
        int size = buffer.size();
        while (readPos < 0) readPos += size;
        while (readPos >= size) readPos -= size;
        
        int idx1 = (int)readPos;
        int idx2 = (idx1 + 1) % size;
        float frac = readPos - idx1;
        return buffer[idx1] * (1.f - frac) + buffer[idx2] * frac;
    }
};

// ---------------------------------------------------------------------------
// 4-Voice Granular Pitch Shifter (+1 Octave)
// ---------------------------------------------------------------------------
struct PitchShifter {
    std::vector<float> buffer;
    int writePos = 0;
    float phase = 0.f;
    float grainSize = 1000.f;

    void init(float sr) {
        buffer.assign((size_t)(sr * 0.1f), 0.f);
        grainSize = sr * 0.025f; // 25ms grains for tight tracking
        writePos = 0;
        phase = 0.f;
    }

    float process(float in) {
        if (buffer.empty()) return in;
        int size = buffer.size();
        buffer[writePos] = in;
        
        float rate = 1.0f / grainSize;
        phase += rate;
        if (phase >= 1.0f) phase -= 1.0f;
        
        auto readInterp = [&](float p) {
            float distance = (1.0f - p) * grainSize;
            float readPos = (float)writePos - distance;
            while (readPos < 0) readPos += size;
            while (readPos >= size) readPos -= size;
            
            int idx1 = (int)readPos;
            int idx2 = (idx1 + 1) % size;
            float frac = readPos - idx1;
            return buffer[idx1] * (1.f - frac) + buffer[idx2] * frac;
        };

        // 4 overlapping grains spaced by 0.25
        float p1 = phase;
        float p2 = phase + 0.25f; if (p2 >= 1.f) p2 -= 1.f;
        float p3 = phase + 0.50f; if (p3 >= 1.f) p3 -= 1.f;
        float p4 = phase + 0.75f; if (p4 >= 1.f) p4 -= 1.f;

        // Hann windows (Sum = 2.0 perfectly)
        float e1 = 0.5f - 0.5f * std::cos(2.f * M_PI * p1);
        float e2 = 0.5f - 0.5f * std::cos(2.f * M_PI * p2);
        float e3 = 0.5f - 0.5f * std::cos(2.f * M_PI * p3);
        float e4 = 0.5f - 0.5f * std::cos(2.f * M_PI * p4);

        float out = (readInterp(p1) * e1 + 
                     readInterp(p2) * e2 +
                     readInterp(p3) * e3 +
                     readInterp(p4) * e4) * 0.5f; // Normalize sum back to 1.0

        writePos++;
        if (writePos >= size) writePos = 0;
        return out;
    }
};

// ---------------------------------------------------------------------------
// Lush FDN Shimmer Reverb Engine
// ---------------------------------------------------------------------------
struct Shimmer {
    ModDelayLine d1, d2, d3, d4;
    Allpass ap1, ap2;
    OnePoleLPF damp1, damp2, damp3, damp4;
    OnePoleLPF bandPassLpf;
    OnePoleHPF bandPassHpf;
    PitchShifter pitch;
    
    float peakEnv = 0.f;
    float attackState = 0.f;
    float sampleRate = 44100.f;

    void setSampleRate(float sr) {
        sampleRate = sr;
        
        // FDN delay lengths in ms (mutually prime to prevent ringing)
        // Includes slow, deep LFO modulation for chorusing/blurring the tail
        d1.init(37.3f, 0.73f, 15.f, sr);
        d2.init(41.1f, 0.91f, 15.f, sr);
        d3.init(43.9f, 1.13f, 15.f, sr);
        d4.init(47.7f, 1.37f, 15.f, sr);
        
        // Input diffusers
        ap1.init(5.3f, 0.6f, sr);
        ap2.init(8.7f, 0.6f, sr);

        pitch.init(sr);
        
        // Strict Shimmer Bandpass (Filters out low mud and high metallic fizz)
        bandPassLpf.setCutoff(3000.f, sr);
        bandPassHpf.setCutoff(500.f, sr);
    }
    
    void reset() {
        std::fill(d1.buffer.begin(), d1.buffer.end(), 0.f);
        std::fill(d2.buffer.begin(), d2.buffer.end(), 0.f);
        std::fill(d3.buffer.begin(), d3.buffer.end(), 0.f);
        std::fill(d4.buffer.begin(), d4.buffer.end(), 0.f);
        std::fill(ap1.buffer.begin(), ap1.buffer.end(), 0.f);
        std::fill(ap2.buffer.begin(), ap2.buffer.end(), 0.f);
        std::fill(pitch.buffer.begin(), pitch.buffer.end(), 0.f);
        peakEnv = 0.f;
        attackState = 0.f;
    }

    float process(float in, float shimmerMix, float decay, float tone, float delaySamples, float attackTime) {
        // (delaySamples is ignored because this is now a dedicated FDN reverb tank, not a delay line)
        
        // 1. Swell envelope processing for the shimmer input
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
        
        // 2. Input Diffusion
        shimmerIn = ap1.process(shimmerIn);
        shimmerIn = ap2.process(shimmerIn);
        
        // 3. Reverb Damping (controlled by the Tone knob)
        float dampFreq = 500.f + tone * 12000.f;
        damp1.setCutoff(dampFreq, sampleRate);
        damp2.setCutoff(dampFreq, sampleRate);
        damp3.setCutoff(dampFreq, sampleRate);
        damp4.setCutoff(dampFreq, sampleRate);

        // 4. Read from the FDN Delay Lines
        float o1 = d1.read();
        float o2 = d2.read();
        float o3 = d3.read();
        float o4 = d4.read();

        // 5. Create a mix for the Pitch Shifter, then bandpass and saturate it
        float fdnMix = (o1 + o2 + o3 + o4) * 0.25f;
        
        float pitched = pitch.process(fdnMix);
        pitched = bandPassHpf.process(pitched);
        pitched = bandPassLpf.process(pitched);
        pitched = std::tanh(pitched / 5.f) * 5.f; // Safety clipping

        // 6. Hadamard Feedback Matrix (Lossless energy scattering)
        float decayAmt = decay * 0.98f; // Max decay is slightly below 1.0 to guarantee stability
        
        float in1 = shimmerIn + pitched + (o1 + o2 + o3 + o4) * 0.5f * decayAmt;
        float in2 = shimmerIn + pitched + (o1 - o2 + o3 - o4) * 0.5f * decayAmt;
        float in3 = shimmerIn + pitched + (o1 + o2 - o3 - o4) * 0.5f * decayAmt;
        float in4 = shimmerIn + pitched + (o1 - o2 - o3 + o4) * 0.5f * decayAmt;
        
        // 7. Write back into the Delay Lines with damping and denormal-prevention
        d1.write(damp1.process(in1) + 1e-9f);
        d2.write(damp2.process(in2) + 1e-9f);
        d3.write(damp3.process(in3) + 1e-9f);
        d4.write(damp4.process(in4) + 1e-9f);

        // Mix the washed-out FDN tail back with the dry signal
        return in + fdnMix * shimmerMix;
    }
};
