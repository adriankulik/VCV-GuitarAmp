#include <iostream>
#include <cassert>
#include <cmath>

// DSP Headers
#include "../src/Drive.hpp"

int main() {
    std::cout << "Running Drive test..." << std::endl;
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
    return 0;
}
