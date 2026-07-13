#include <iostream>
#include <cassert>
#include <cmath>

// DSP Headers
#include "../src/Biquad.hpp"

int main() {
    std::cout << "Running Biquad test..." << std::endl;
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
    return 0;
}
