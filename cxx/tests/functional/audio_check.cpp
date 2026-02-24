/**
 * @file audio_check.cpp
 * @brief Basic audio driver functional check.
 */

#include <iostream>
#include <vector>
#include <cmath>
#include "TestHelper.hpp"

int main() {
    std::cout << "--- Audio Driver Check ---" << std::endl;

    test::init_test_environment();
    auto driver = test::create_driver();
    if (!driver) return 1;

    double phase = 0.0;
    double freq = 440.0;
    double sr = static_cast<double>(driver->sample_rate());

    driver->set_callback([&phase, freq, sr](std::span<float> output) {
        for (float& sample : output) {
            sample = 0.2f * std::sin(2.0 * M_PI * phase);
            phase += freq / sr;
            if (phase >= 1.0) phase -= 1.0;
        }
    });

    if (!driver->start()) {
        return 1;
    }

    std::cout << "Playing 440Hz sine wave for 2 seconds (Ctrl+C to stop early)..." << std::endl;
    test::wait_while_running(2);

    test::cleanup_test_environment(driver.get());
    std::cout << "--- Done ---" << std::endl;

    return 0;
}
