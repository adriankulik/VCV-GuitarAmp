#include <iostream>
#include <cassert>
#include <cmath>

// DSP Headers
#include "../src/NoiseGate.hpp"

int main() {
    std::cout << "Running NoiseGate test..." << std::endl;
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
    return 0;
}
