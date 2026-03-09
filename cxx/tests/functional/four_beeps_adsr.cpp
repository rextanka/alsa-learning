#include "../TestHelper.hpp"
#include <iostream>
#include <vector>
#include <thread>
#include <chrono>
#include <cassert>

/**
 * @file four_beeps_adsr.cpp
 * @brief Plays 4 beeps, 1 second apart, descending in pitch to verify ADSR. 
 */
int main() {
    test::init_test_environment();
    int sample_rate = test::get_safe_sample_rate(0);

    PRINT_TEST_HEADER(
        "Validate ADSR envelope articulation and voice lifecycle.",
        "Pulse Oscillator (50% PW) -> VCA (ADSR) -> Output.",
        "Four distinct pulse beeps with audible attack/decay and no clicks.",
        "4 distinct ADSR-shaped tones (C4, B3, A3, G3), 1 second apart.",
        sample_rate
    );

    test::EngineWrapper engine(sample_rate);

    // 1. Configure Recipe 2: Articulated Mono
    // VCO Isolation: Pulse @ 0.5 width
    set_param(engine.get(), "pulse_gain", 0.7f);
    set_param(engine.get(), "pulse_width", 0.5f); // Square Wave
    
    // Explicitly zero out other oscillators
    set_param(engine.get(), "saw_gain", 0.0f);
    set_param(engine.get(), "sub_gain", 0.0f);
    
    // VCF Open: Cutoff fully open, resonance zeroed
    set_param(engine.get(), "vcf_cutoff", 20000.0f);
    set_param(engine.get(), "vcf_res", 0.0f);

    // VCA (ADSR) Configuration - Using engine_connect_mod as per TEST_DESC.md Tier 2 protocol
    assert(engine_connect_mod(engine.get(), MOD_SRC_ENVELOPE, ALL_VOICES, MOD_TGT_AMPLITUDE, 1.0f) == 0);
    assert(set_param(engine.get(), "amp_attack", 0.050f) == 0);  // 50ms fade in
    assert(set_param(engine.get(), "amp_decay", 0.100f) == 0);   // 100ms decay
    assert(set_param(engine.get(), "amp_sustain", 0.8f) == 0);   // Hold at 80% volume
    assert(set_param(engine.get(), "amp_release", 0.100f) == 0);  // 100ms fade out

    if (engine_start(engine.get()) != 0) {
        std::cerr << "Failed to start audio engine" << std::endl;
        return 1;
    }

    // 2. Define descending notes (C4, B3, A3, G3)
    const char* notes[] = {"C4", "B3", "A3", "G3"};
    
    for (int i = 0; i < 4; ++i) {
        std::cout << "[BEEP " << (i + 1) << "] Playing " << notes[i] << "..." << std::endl;
        
        // Trigger Note On
        engine_note_on_name(engine.get(), notes[i], 0.8f);
        
        // Hold for 0.5 seconds
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        
        // Trigger Note Off (Triggers Release phase)
        engine_note_off_name(engine.get(), notes[i]);
        
        // Wait for the remainder of the 1-second interval
        if (i < 3) {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
    }

    // Wait slightly for the final release to complete
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    engine_stop(engine.get());
    std::cout << "--- Test Completed. Engine destroyed via RAII. ---" << std::endl;
    return 0;
}
