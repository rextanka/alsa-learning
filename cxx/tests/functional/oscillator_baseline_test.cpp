/**
 * @file oscillator_baseline_test.cpp
 * @brief Direct hardware-to-oscillator validation bypassing the Voice/Graph layers.
 */

#include <iostream>
#include <vector>
#include <cmath>
#include <memory>
#include <chrono>
#include "TestHelper.hpp"
#include "oscillator/SineOscillatorProcessor.hpp"

int main() {
    std::cout << "--- Starting Oscillator Baseline Validation (48kHz) ---" << std::endl;

    test::init_test_environment();
    // CoreAudioDriver enforces 48000Hz internally
    auto driver = test::create_driver();
    if (!driver) return 1;

    std::cout << "Driver sample rate: " << driver->sample_rate() << " Hz" << std::endl;
    
    // 1. Instantiate the actual engine oscillator
    auto sine_osc = std::make_unique<audio::SineOscillatorProcessor>(48000);
    sine_osc->set_frequency(440.0);

    // 2. Setup Driver Callback to verify our Oscillator Processor
    driver->set_callback([&](std::span<float> output) {
        // Stage 2: Verify our Oscillator Processor
        sine_osc->pull(output, nullptr);
        
        // Quick Audit: Monitor for silence or clipping
        static int audit_counter = 0;
        if (audit_counter++ % 100 == 0) {
            float peak = 0.0f;
            for (float s : output) if (std::abs(s) > peak) peak = std::abs(s);
            std::cout << "[OSC AUDIT] Peak: " << peak << std::endl;
        }
    });

    if (!driver->start()) return 1;

    std::cout << "Playing SineOscillatorProcessor (440Hz) for 5 seconds..." << std::endl;
    
    test::wait_while_running(5);

    test::cleanup_test_environment(driver.get());
    std::cout << "--- Baseline Verification Done ---" << std::endl;
    return 0;
}
