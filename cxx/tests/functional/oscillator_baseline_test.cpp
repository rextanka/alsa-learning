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
#include "oscillator/PulseOscillatorProcessor.hpp"
#include "oscillator/SubOscillator.hpp"

using namespace audio;

int main() {
    std::cout << "--- Starting Oscillator Baseline Validation ---" << std::endl;

    test::init_test_environment();
    auto driver = test::create_driver();
    if (!driver) return 1;

    int sample_rate = driver->sample_rate();
    
    // 1. Instantiate Oscillators directly
    auto pulse_osc = std::make_unique<PulseOscillatorProcessor>(sample_rate);
    auto sub_osc = std::make_unique<SubOscillator>();

    // Set stable frequencies
    pulse_osc->set_frequency(440.0); // A4
    pulse_osc->set_pulse_width(0.5f); // Square wave

    // 2. Setup Driver Callback to cycle through oscillators
    int test_phase = 0; // 0: Pulse, 1: Sub, 2: Mixed
    auto start_time = std::chrono::steady_clock::now();

    driver->set_callback([&](std::span<float> output) {
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now() - start_time).count();
        
        // Cycle every 3 seconds
        test_phase = (elapsed / 3) % 3;

        // Temporary block buffer
        std::vector<float> pulse_buffer(output.size());
        
        // PULL: Use block-based pull to ensure phase continuity across the buffer
        pulse_osc->pull(pulse_buffer, nullptr);

        for (size_t i = 0; i < output.size(); ++i) {
            float pulse_sample = pulse_buffer[i];
            // Locked Sub-oscillator tracking
            float sub_sample = static_cast<float>(sub_osc->generate_sample(pulse_osc->get_phase()));

            if (test_phase == 0) {
                output[i] = 0.2f * pulse_sample; // Pulse Only
            } else if (test_phase == 1) {
                output[i] = 0.2f * sub_sample;   // Sub Only
            } else {
                output[i] = 0.15f * (pulse_sample + sub_sample); // Mixed
            }
        }
    });

    if (!driver->start()) return 1;

    std::cout << "0-3s: Pulse Only (A4 Square)" << std::endl;
    std::cout << "3-6s: Sub Only (A3/A2)" << std::endl;
    std::cout << "6-9s: Mixed" << std::endl;
    
    test::wait_while_running(9);

    test::cleanup_test_environment(driver.get());
    std::cout << "--- Baseline Test Done ---" << std::endl;
    return 0;
}
