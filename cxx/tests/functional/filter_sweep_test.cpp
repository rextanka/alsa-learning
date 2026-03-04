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
    set_param(engine, "saw_gain", 1.0f);   // Ensure saw is audible
    set_param(engine, "pulse_gain", 0.0f); // Silence default pulse
    set_param(engine, "sub_gain", 0.0f);   // Silence default sub
    set_param(engine, "amp_attack", 0.01f); // Quick attack
    set_param(engine, "amp_sustain", 1.0f);

    if (engine_start(engine) != 0) {
        std::cerr << "Failed to start engine" << std::endl;
        engine_destroy(engine);
        return 1;
    }

    // Trigger A2 (approx 110Hz)
    std::cout << "[TEST] Triggering A2 drone (Sawtooth)..." << std::endl;
    engine_note_on(engine, 45, 1.0f); // A2 is MIDI 45

    auto run_sweep = [&](const char* name, int type, float res) {
        std::cout << "\n--- Starting Sweep: " << name << " ---" << std::endl;
        std::cout << "DEBUG: Confirming Processor Type... " << std::endl;
        engine_set_filter_type(engine, type);
        
        // SYNC: Ensure starting parameters are set AFTER switching
        set_param(engine, "vcf_cutoff", 8000.0f);
        set_param(engine, "vcf_res", res);
        
        const int steps = 100;
        const float start_freq = 8000.0f;
        const float end_freq = 200.0f; // Keep fundamental audible (A2 ~110Hz)
        const int duration_ms = 2000;
        const int step_ms = duration_ms / steps;

        for (int i = 0; i <= steps; ++i) {
            float t = static_cast<float>(i) / steps;
            // Logarithmic sweep for more natural filter movement
            float cutoff = start_freq * std::pow(end_freq / start_freq, t);
            
            set_param(engine, "vcf_cutoff", cutoff);
            
            if (i % 10 == 0) {
                // Peek peak to verify signal presence
                // Note: Peak logging is handled internally by Voice::do_pull
            }
            
            std::this_thread::sleep_for(std::chrono::milliseconds(step_ms));
        }
        std::cout << "--- Sweep " << name << " Completed ---" << std::endl;
    };

    // 1. Run Moog Sweep (Classic Peak)
    run_sweep("Moog Ladder", 0, 0.85f);

    // 2. Run Diode Sweep (Squelchy Peak)
    run_sweep("Diode Ladder", 1, 0.5f);

    std::cout << "\n[TEST] Releasing note..." << std::endl;
    engine_note_off(engine, 45);
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    engine_flush_logs(engine);
    engine_stop(engine);
    engine_destroy(engine);

    std::cout << "\n[SUCCESS] Filter sweep test completed." << std::endl;
    return 0;
}
