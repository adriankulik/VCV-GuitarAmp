#include <iostream>
#include <cassert>
#include <cmath>

// DSP Headers
#include "../src/Shimmer.hpp"

int main() {
    std::cout << "Running Shimmer test..." << std::endl;
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
    return 0;
}
