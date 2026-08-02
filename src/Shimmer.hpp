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
        float v = in + gain * delayed;
        float out = -gain * v + delayed;
        buffer[writePos] = v;
        
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
    float baseLengthSamples = 0.f;
    float maxLengthSamples = 0.f;
    float lfoPhase = 0.f;
    float lfoFreq = 1.f;
    float lfoDepth = 1.f;

    void init(float lengthMs, float lfoF, float lfoD, float sr) {
        baseLengthSamples = lengthMs * 0.001f * sr;
        maxLengthSamples = baseLengthSamples * 2.0f; // Allow up to 2x size
        buffer.assign((size_t)(maxLengthSamples + sr * 0.1f), 0.f); 
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

    float read(float sizeScalar) {
        if (buffer.empty()) return 0.f;
        
        lfoPhase += lfoFreq;
        if (lfoPhase >= 1.f) lfoPhase -= 1.f;
        
        // Slow, deep modulation
        float mod = std::sin(2.f * M_PI * lfoPhase) * lfoDepth;
        float currentLength = baseLengthSamples * sizeScalar;
        float readPos = (float)writePos - currentLength - mod;
        
        int size = buffer.size();
        while (readPos < 0) readPos += size;
        while (readPos >= size) readPos -= size;
        
        int idx1 = (int)readPos;
        int idx2 = (idx1 + 1) % size;
        int idx0 = (idx1 - 1 + size) % size;
        int idx3 = (idx2 + 1) % size;
        
        float frac = readPos - (float)idx1;
        
        // Hermite Interpolation (Preserves high frequencies)
        float y0 = buffer[idx0];
        float y1 = buffer[idx1];
        float y2 = buffer[idx2];
        float y3 = buffer[idx3];
        
        float c0 = y1;
        float c1 = 0.5f * (y2 - y0);
        float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
        float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
        
        return ((c3 * frac + c2) * frac + c1) * frac + c0;
    }
};

// ---------------------------------------------------------------------------
// 4-Voice Granular Pitch Shifter (Variable Shift)
// ---------------------------------------------------------------------------
struct PitchShifter {
    std::vector<float> buffer;
    int writePos = 0;
    float phase = 0.f;
    float grainSize = 1000.f;

    void init(float sr) {
        buffer.assign((size_t)(sr * 0.1f), 0.f);
        grainSize = sr * 0.085f; // 85ms grains for much smoother shimmer clouds
        writePos = 0;
        phase = 0.f;
    }

    float process(float in, float shiftSemitones) {
        if (buffer.empty()) return in;
        int size = buffer.size();
        buffer[writePos] = in;
        
        float rate = 1.0f / grainSize;
        phase += rate;
        if (phase >= 1.0f) phase -= 1.0f;
        
        float pitchRatio = std::pow(2.0f, shiftSemitones / 12.0f);
        float D = (pitchRatio - 1.0f) * grainSize;
        float offset = (D < 0.f) ? -D : 0.f; // Guarantee causal reading
        
        auto readInterp = [&](float p) {
            float distance = (1.0f - p) * D + offset;
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
    Allpass tankAp1, tankAp2, tankAp3, tankAp4;
    OnePoleLPF damp1, damp2, damp3, damp4;
    OnePoleLPF bandPassLpf;
    OnePoleHPF bandPassHpf;
    PitchShifter pitch;
    
    float peakEnv = 0.f;
    float attackState = 0.f;
    float sampleRate = 44100.f;

    void setSampleRate(float sr) {
        sampleRate = sr;
        
        // 1. MASSIVELY longer FDN delay lengths (Primes)
        d1.init(113.1f, 0.73f, 15.f, sr);
        d2.init(151.3f, 0.91f, 15.f, sr);
        d3.init(173.9f, 1.13f, 15.f, sr);
        d4.init(199.7f, 1.37f, 15.f, sr);
        
        // 2. Input diffusers
        ap1.init(13.3f, 0.6f, sr);
        ap2.init(19.7f, 0.6f, sr);

        // 3. TANK Diffusers (Crucial for smearing pitch grains)
        tankAp1.init(23.3f, 0.6f, sr);
        tankAp2.init(29.7f, 0.6f, sr);
        tankAp3.init(31.1f, 0.6f, sr);
        tankAp4.init(37.3f, 0.6f, sr);

        pitch.init(sr); // 85ms grains in PitchShifter
        
        // Strict Shimmer Bandpass
        bandPassLpf.setCutoff(4000.f, sr);
        bandPassHpf.setCutoff(150.f, sr); // Lowered HPF to let fundamentals through
    }
    
    void reset() {
        std::fill(d1.buffer.begin(), d1.buffer.end(), 0.f);
        std::fill(d2.buffer.begin(), d2.buffer.end(), 0.f);
        std::fill(d3.buffer.begin(), d3.buffer.end(), 0.f);
        std::fill(d4.buffer.begin(), d4.buffer.end(), 0.f);
        std::fill(ap1.buffer.begin(), ap1.buffer.end(), 0.f);
        std::fill(ap2.buffer.begin(), ap2.buffer.end(), 0.f);
        std::fill(tankAp1.buffer.begin(), tankAp1.buffer.end(), 0.f);
        std::fill(tankAp2.buffer.begin(), tankAp2.buffer.end(), 0.f);
        std::fill(tankAp3.buffer.begin(), tankAp3.buffer.end(), 0.f);
        std::fill(tankAp4.buffer.begin(), tankAp4.buffer.end(), 0.f);
        std::fill(pitch.buffer.begin(), pitch.buffer.end(), 0.f);
        peakEnv = 0.f;
        attackState = 0.f;
    }

    std::pair<float, float> process(float in, float shimmerMix, float decay, float sizeParam, float toneParam, float attackTime) {
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
        
        // 3. Reverb Damping (Controlled by Tone param to reduce metallic resonances)
        float dampFreq = 1000.f + toneParam * 11000.f;
        damp1.setCutoff(dampFreq, sampleRate);
        damp2.setCutoff(dampFreq, sampleRate);
        damp3.setCutoff(dampFreq, sampleRate);
        damp4.setCutoff(dampFreq, sampleRate);

        // Map Size knob (0..1) to size multiplier (0.1x to 2.0x)
        float sizeScalar = 0.1f + sizeParam * 1.9f;

        // 4. Read from the FDN Delay Lines with interpolated size changes
        float o1 = d1.read(sizeScalar);
        float o2 = d2.read(sizeScalar);
        float o3 = d3.read(sizeScalar);
        float o4 = d4.read(sizeScalar);

        // Diffuse inside the tank BEFORE the pitch shifter
        o1 = tankAp1.process(o1);
        o2 = tankAp2.process(o2);
        o3 = tankAp3.process(o3);
        o4 = tankAp4.process(o4);

        // 5. Pitch shift one of the tank lines (o1) to create the shimmer safely (fixed at +1 octave)
        float pitched = pitch.process(o1, 12.f);
        pitched = bandPassHpf.process(pitched);
        pitched = bandPassLpf.process(pitched);
        pitched = std::tanh(pitched / 5.f) * 5.f; // Safety clipping
        
        // REPLACES o1 to keep feedback matrix perfectly orthogonal!
        o1 = pitched; 
        

        // 6. Hadamard Feedback Matrix (Lossless energy scattering)
        float decayAmt = decay * 0.98f; // Max decay is slightly below 1.0 to guarantee stability
        
        // Standard Hadamard Matrix
        float in1 = shimmerIn + (o1 + o2 + o3 + o4) * 0.5f * decayAmt;
        float in2 = shimmerIn + (o1 - o2 + o3 - o4) * 0.5f * decayAmt;
        float in3 = shimmerIn + (o1 + o2 - o3 - o4) * 0.5f * decayAmt;
        float in4 = shimmerIn + (o1 - o2 - o3 + o4) * 0.5f * decayAmt;
        
        // 7. Write back into the Delay Lines with damping and denormal-prevention
        static float antiDenormal = 1e-9f;
        antiDenormal = -antiDenormal;

        d1.write(damp1.process(in1) + antiDenormal);
        d2.write(damp2.process(in2) + antiDenormal);
        d3.write(damp3.process(in3) + antiDenormal);
        d4.write(damp4.process(in4) + antiDenormal);

        // Mix the washed-out FDN tail back with the dry signal, panned for stereo width
        float outL = in + (o1 + o3) * shimmerMix;
        float outR = in + (o2 + o4) * shimmerMix;
        return {outL, outR};
    }
};
