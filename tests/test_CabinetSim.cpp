#include <iostream>
#include <cassert>
#include <cmath>

// DSP Headers
#include "../src/CabinetSim.hpp"

int main() {
    std::cout << "Running CabinetSim test..." << std::endl;
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
    return 0;
}
