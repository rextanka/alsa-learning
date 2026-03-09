/**
 * @file filter_sweep_test.cpp
 * @brief Functional test of Moog and Diode filters using a Sawtooth drone and a frequency sweep. 
 */

#include "../TestHelper.hpp"
#include <iostream>
#include <chrono>
#include <thread>
#include <cmath>

int main() {
    test::init_test_environment();
    int sample_rate = test::get_safe_sample_rate(0);

    PRINT_TEST_HEADER(
        "Validate resonant filter sweep and tag-based parameter smoothing.",
        "Validates spectral character and resonance of Moog and Diode ladder filters.",
        "Sawtooth -> VCF (Moog) -> VCA -> Output",
        "Smooth 2-octave resonant sweep without audible stepping.",
        sample_rate
    );

    test::EngineWrapper engine(sample_rate);

    // Set Up Signal Chain
    set_param(engine.get(), "saw_gain", 1.0f);
    set_param(engine.get(), "amp_sustain", 1.0f);

    if (engine_start(engine.get()) != 0) {
        std::cerr << "Failed to start engine" << std::endl;
        return 1;
    }

    // Trigger A2 (approx 110Hz)
    std::cout << "[TEST] Triggering A2 drone (Sawtooth)..." << std::endl;
    engine_note_on(engine.get(), 45, 1.0f); // A2 is MIDI 45

    auto run_sweep = [&](const char* name, int type, float res) {
        std::cout << "\n--- Starting Sweep: " << name << " ---" << std::endl;
        engine_set_filter_type(engine.get(), type);
        
        // SYNC: Ensure starting parameters are set AFTER switching
        set_param(engine.get(), "vcf_cutoff", 8000.0f);
        set_param(engine.get(), "vcf_res", res);
        
        // High density sweep: 200 steps over 2 seconds (10ms steps)
        // This exercises the parameter smoothing and avoids audible stepping.
        const int steps = 200;
        const float start_freq = 8000.0f;
        const float end_freq = 100.0f;
        const int duration_ms = 2000;
        const int step_ms = duration_ms / steps;

        for (int i = 0; i <= steps; ++i) {
            float t = static_cast<float>(i) / steps;
            // Logarithmic sweep for more natural filter movement
            float cutoff = start_freq * std::pow(end_freq / start_freq, t);
            
            set_param(engine.get(), "vcf_cutoff", cutoff);
            std::this_thread::sleep_for(std::chrono::milliseconds(step_ms));
        }
        std::cout << "--- Sweep " << name << " Completed ---" << std::endl;
    };

    // 1. Run Moog Sweep (Classic Peak)
    // Resonance set high (0.85f) to ensure audible sweep as requested
    run_sweep("Moog Ladder", 0, 0.85f);

    // 2. Run Diode Sweep (Squelchy Peak)
    run_sweep("Diode Ladder", 1, 0.7f);

    std::cout << "\n[TEST] Releasing note..." << std::endl;
    engine_note_off(engine.get(), 45);
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    engine_stop(engine.get());

    std::cout << "\n[SUCCESS] Filter sweep test completed. Engine released via RAII." << std::endl;
    return 0;
}
