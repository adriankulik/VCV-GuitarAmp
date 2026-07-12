#include <iostream>
#include <cassert>
#include <cmath>

// DSP Headers
#include "../src/Biquad.hpp"
#include "../src/Drive.hpp"
#include "../src/NoiseGate.hpp"
#include "../src/Shimmer.hpp"
#include "../src/CabinetSim.hpp"

void testBiquad() {
    float sr = 44100.f;
    Biquad eq;
    
    // Test LowShelf at 250 Hz with +12 dB
    eq.setLowShelf(250.f, 12.f, sr);
    
    // +12dB LowShelf should amplify DC (0 Hz) by exactly +12dB (approx 3.98x)
    eq.reset();
    float dcOut = 0.f;
    for (int i = 0; i < 10000; ++i) {
        dcOut = eq.process(1.0f);
    }
    assert(dcOut > 3.9f && dcOut < 4.1f);
    
    std::cout << "Biquad test passed." << std::endl;
}

void testDrive() {
    // Soft clip (overdrive) compresses dynamic range
    float outSoftLow = applyDrive(0.1f, 1.0f, 0);
    float outSoftHigh = applyDrive(1.0f, 1.0f, 0);
    // 10x input should NOT result in 10x output
    assert(outSoftHigh < outSoftLow * 10.f);
    
    // Hard clip (distortion) should clamp signal strictly to [-1, 1]
    float outHard = applyDrive(2.0f, 1.0f, 1);
    assert(std::abs(outHard) <= 1.0f);
    
    // Asymmetric Fuzz should treat positive and negative swings differently
    float fuzzPos = applyDrive(0.02f, 1.0f, 2);
    float fuzzNeg = applyDrive(-0.02f, 1.0f, 2);
    assert(std::abs(fuzzPos) != std::abs(fuzzNeg));

    std::cout << "Drive test passed." << std::endl;
}

void testNoiseGate() {
    NoiseGate gate;
    float sr = 44100.f;
    
    // Signal below threshold (0.1) should eventually be gated to zero
    float outQuiet = 0.f;
    for(int i=0; i<10000; ++i) {
        outQuiet = gate.process(0.05f, 0.1f, 5.f, 10.f, sr);
    }
    assert(outQuiet < 0.01f);
    
    // Signal above threshold should remain unaffected (output ~ input)
    float outLoud = 0.f;
    for(int i=0; i<10000; ++i) {
        outLoud = gate.process(0.8f, 0.1f, 5.f, 10.f, sr);
    }
    assert(outLoud > 0.7f);
    
    std::cout << "NoiseGate test passed." << std::endl;
}

void testShimmer() {
    Shimmer shimmer;
    float sr = 44100.f;
    shimmer.setSampleRate(sr);
    
    // Test Shimmer Mix: with 0.0 mix, the output must equal the exact input
    float outDry = shimmer.process(0.5f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f);
    assert(outDry == 0.5f);
    
    // Test Attack Swell (2.0 seconds attack time)
    shimmer.reset();
    float firstSample = shimmer.process(1.0f, 1.0f, 0.5f, 1.0f, 0.0f, 2.0f); 
    // Since attack is slow, the wet signal (feedback) is 0 at the first sample
    assert(firstSample == 1.0f); 
    
    // Run enough samples to let the swell envelope rise and feed the buffer
    float laterSample = 0.f;
    for (int i = 0; i < 44100; ++i) {
        laterSample = shimmer.process(1.0f, 1.0f, 0.5f, 1.0f, 0.0f, 2.0f);
    }
    // Now the wet signal has ramped up, adding to the dry signal
    assert(laterSample > 1.0f);
    
    std::cout << "Shimmer test passed." << std::endl;
}

void testCabinetSim() {
    CabinetSim cab;
    float sr = 44100.f;
    cab.setSampleRate(sr);
    
    // The Cabinet sim has a highpass filter at 80Hz to kill DC/sub.
    // If we feed it a continuous 1.0 DC signal, it should settle at 0.0.
    float dcOut = 0.f;
    for (int i = 0; i < 10000; ++i) {
        dcOut = cab.process(1.0f);
    }
    assert(std::abs(dcOut) < 0.05f);

    std::cout << "CabinetSim test passed." << std::endl;
}

int main() {
    std::cout << "Running Comprehensive DSP Tests..." << std::endl;
    testBiquad();
    testDrive();
    testNoiseGate();
    testShimmer();
    testCabinetSim();
    std::cout << "All Comprehensive DSP tests passed successfully!" << std::endl;
    return 0;
}
