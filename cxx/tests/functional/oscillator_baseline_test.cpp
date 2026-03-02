/**
 * @file oscillator_baseline_test.cpp
 * @brief Direct hardware-to-oscillator validation with ADSR envelope gating.
 */

#include <iostream>
#include <vector>
#include <cmath>
#include <memory>
#include <chrono>
#include "TestHelper.hpp"
#include "oscillator/SineOscillatorProcessor.hpp"
#include "envelope/AdsrEnvelopeProcessor.hpp"

int main() {
    std::cout << "--- Starting Oscillator Baseline Validation (48kHz) ---" << std::endl;

    test::init_test_environment();
    // CoreAudioDriver enforces 48000Hz internally
    auto driver = test::create_driver();
    if (!driver) return 1;

    std::cout << "Driver sample rate: " << driver->sample_rate() << " Hz" << std::endl;
    
    // 1. Instantiate the actual engine oscillator (Explicit 48000Hz)
    auto sine_osc = std::make_unique<audio::SineOscillatorProcessor>(48000);
    sine_osc->set_frequency(440.0);

    // 2. Instantiate the ADSR envelope (Explicit 48000Hz)
    auto envelope = std::make_unique<audio::AdsrEnvelopeProcessor>(48000);
    envelope->set_attack_time(0.05f);   // 50ms
    envelope->set_decay_time(0.1f);     // 100ms
    envelope->set_sustain_level(0.7f);   // 70%
    envelope->set_release_time(0.2f);   // 200ms

    // 3. Setup Driver Callback with 2-second cycling gate
    auto start_time = std::chrono::steady_clock::now();
    bool last_gate = false;

    driver->set_stereo_callback([&, start_time, last_gate](audio::AudioBuffer& buffer) mutable {
        // Calculate gate status based on 2-second cycle (1s ON, 1s OFF)
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time).count();
        bool current_gate = (elapsed % 2000) < 1000;

        if (current_gate != last_gate) {
            if (current_gate) {
                envelope->gate_on();
            } else {
                envelope->gate_off();
            }
            last_gate = current_gate;
        }

        // Pull from Generator (Oscillator) into Left channel
        sine_osc->pull(buffer.left, nullptr);
        
        // Pull from Processor (Envelope) - multiplies in-place
        envelope->pull(buffer.left, nullptr);

        // Copy to Right channel
        for (size_t i = 0; i < buffer.frames(); ++i) {
            buffer.right[i] = buffer.left[i];
        }
        
        // Quick Audit: Monitor for silence or clipping
        static int audit_counter = 0;
        if (audit_counter++ % 100 == 0) {
            float peak = 0.0f;
            for (float s : buffer.left) if (std::abs(s) > peak) peak = std::abs(s);
            std::cout << "[ENV AUDIT] Gate: " << (current_gate ? "ON " : "OFF") 
                      << " | Peak: " << peak << std::endl;
        }
    });

    if (!driver->start()) return 1;

    std::cout << "Playing Sine + ADSR (440Hz, 2s Cycle) for 10 seconds..." << std::endl;
    
    test::wait_while_running(10);

    test::cleanup_test_environment(driver.get());
    std::cout << "--- Baseline Verification Done ---" << std::endl;
    return 0;
}
