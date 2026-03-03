/**
 * @file filter_sweep_test.cpp
 * @brief Functional test of Moog and Diode filters using a Sawtooth drone and a frequency sweep.
 */

#include "CInterface.h"
#include <iostream>
#include <chrono>
#include <thread>
#include <cmath>

int main() {
    std::cout << "Purpose: Functional test of Moog and Diode filters using a Sawtooth drone and a 2-second frequency sweep at 48kHz." << std::endl;
    std::cout << "Verifying the spectral character of Moog and Diode ladder filters via a resonant frequency sweep at 48kHz." << std::endl;

    const unsigned int sample_rate = 48000;
    EngineHandle engine = engine_create(sample_rate);
    if (!engine) {
        std::cerr << "Failed to create engine" << std::endl;
        return 1;
    }

    // Set Up Signal Chain
    set_param(engine, "osc_wave", 3.0f); // OSC_SAWTOOTH
    set_param(engine, "amp_sustain", 1.0f);
    set_param(engine, "vcf_res", 0.75f); // High resonance for audible sweep

    if (engine_start(engine) != 0) {
        std::cerr << "Failed to start engine" << std::endl;
        engine_destroy(engine);
        return 1;
    }

    // Trigger A2 (approx 110Hz)
    std::cout << "[TEST] Triggering A2 drone (Sawtooth)..." << std::endl;
    engine_note_on(engine, 45, 1.0f); // A2 is MIDI 45

    auto run_sweep = [&](const char* name, int type) {
        std::cout << "\n--- Starting Sweep: " << name << " ---" << std::endl;
        engine_set_filter_type(engine, type);
        
        // Ensure starting parameters are set AFTER switching
        set_param(engine, "vcf_cutoff", 8000.0f);
        
        const int steps = 100;
        const float start_freq = 8000.0f;
        const float end_freq = 100.0f;
        const int duration_ms = 2000;
        const int step_ms = duration_ms / steps;

        for (int i = 0; i <= steps; ++i) {
            float t = static_cast<float>(i) / steps;
            // Logarithmic sweep for more natural filter movement
            float cutoff = start_freq * std::pow(end_freq / start_freq, t);
            
            set_param(engine, "vcf_cutoff", cutoff);
            
            if (i % 10 == 0) {
                std::cout << "  [Sweep] " << name << " Cutoff: " << cutoff << " Hz" << std::endl;
            }
            
            std::this_thread::sleep_for(std::chrono::milliseconds(step_ms));
        }
        std::cout << "--- Sweep " << name << " Completed ---" << std::endl;
    };

    // 1. Run Moog Sweep
    run_sweep("Moog Ladder", 0);

    // 2. Run Diode Sweep
    std::cout << "DEBUG: Diode Cutoff check..." << std::endl;
    set_param(engine, "vcf_res", 0.5f); // Reduced resonance for Diode debugging
    run_sweep("Diode Ladder", 1);

    std::cout << "\n[TEST] Releasing note..." << std::endl;
    engine_note_off(engine, 45);
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    engine_stop(engine);
    engine_destroy(engine);

    std::cout << "\n[SUCCESS] Filter sweep test completed." << std::endl;
    return 0;
}
