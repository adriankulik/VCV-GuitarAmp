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
    Biquad eq;
    eq.setLowShelf(250.f, 6.f, 44100.f);
    float out = eq.process(1.0f);
    assert(out != 0.f);
    std::cout << "Biquad test passed." << std::endl;
}

void testDrive() {
    float outSoft = applyDrive(0.5f, 0.5f, 0); // overdrive
    float outHard = applyDrive(0.5f, 0.5f, 1); // distortion
    float outFuzz = applyDrive(0.5f, 0.5f, 2); // fuzz
    assert(outSoft > 0.f);
    assert(outHard > 0.f);
    assert(outFuzz > 0.f);
    std::cout << "Drive test passed." << std::endl;
}

void testNoiseGate() {
    NoiseGate gate;
    float sampleRate = 44100.f;
    // Process a loud signal (above threshold)
    float out1 = gate.process(0.8f, 0.1f, 5.f, 100.f, sampleRate);
    // Over time it should open up
    for(int i=0; i<1000; ++i) {
        out1 = gate.process(0.8f, 0.1f, 5.f, 100.f, sampleRate);
    }
    assert(out1 > 0.1f);
    std::cout << "NoiseGate test passed." << std::endl;
}

void testShimmer() {
    Shimmer shimmer;
    shimmer.setSampleRate(44100.f);
    // process(in, mix, decay, tone)
    float out = shimmer.process(0.5f, 0.5f, 0.6f, 0.5f, 0.0f, 0.0f);
    assert(out != 0.f);
    std::cout << "Shimmer test passed." << std::endl;
}

void testCabinetSim() {
    CabinetSim cab;
    cab.setSampleRate(44100.f);
    float out = cab.process(0.5f);
    assert(out != 0.f);
    std::cout << "CabinetSim test passed." << std::endl;
}

int main() {
    std::cout << "Running DSP Tests..." << std::endl;
    testBiquad();
    testDrive();
    testNoiseGate();
    testShimmer();
    testCabinetSim();
    std::cout << "All DSP tests passed successfully!" << std::endl;
    return 0;
}
