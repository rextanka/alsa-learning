/**
 * @file oscillator_integrity_test.cpp
 * @brief Solo test for individual oscillators to verify they can make sound via Bridge API.
 */

#include "../TestHelper.hpp"
#include <iostream>
#include <thread>
#include <chrono>

void run_solo_test(EngineHandle engine, const char* name, const char* label, float val) {
    std::cout << "\n>>> SOLO TEST: " << name << " <<<" << std::endl;
    
    // Reset all gains
    set_param(engine, "pulse_gain", 0.0f);
    set_param(engine, "sub_gain", 0.0f);
    set_param(engine, "saw_gain", 0.0f);
    
    // Set target gain
    if (label) {
        set_param(engine, label, val);
    }
    
    std::cout << "Playing Middle C (C4) for 2 seconds..." << std::endl;
    engine_note_on_name(engine, "C4", 0.8f);
    std::this_thread::sleep_for(std::chrono::seconds(2));
    engine_note_off_name(engine, "C4");
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
}

int main() {
    test::init_test_environment();
    int sample_rate = test::get_safe_sample_rate(0);

    PRINT_TEST_HEADER(
        "Oscillator Solo Integrity",
        "Verifies solo/mute behavior of individual oscillators.",
        "Individual VCOs -> VCA -> Output",
        "Discrete tones for Pulse, Sub, Saw, and then a Combined Mix.",
        sample_rate
    );

    test::EngineWrapper engine(sample_rate);

    // Force path clearance - Using engine_connect_mod as per TEST_DESC.md Tier 2 protocol
    engine_connect_mod(engine.get(), MOD_SRC_ENVELOPE, ALL_VOICES, MOD_TGT_AMPLITUDE, 1.0f);
    set_param(engine.get(), "amp_sustain", 1.0f);
    set_param(engine.get(), "vcf_cutoff", 5000.0f);

    if (engine_start(engine.get()) != 0) {
        std::cerr << "Failed to start audio engine" << std::endl;
        return 1;
    }

    // Step 1: Main Pulse Solo
    run_solo_test(engine.get(), "Main Pulse (Square)", "pulse_gain", 1.0f);

    // Step 2: Sub Only
    run_solo_test(engine.get(), "Sub Oscillator (Locked)", "sub_gain", 1.0f);

    // Step 3: Sawtooth
    run_solo_test(engine.get(), "Sawtooth Oscillator", "saw_gain", 1.0f);

    // Step 4: Combined Mix
    std::cout << "\n>>> SOLO TEST: Combined Mix <<<" << std::endl;
    set_param(engine.get(), "pulse_gain", 1.0f);
    set_param(engine.get(), "sub_gain", 0.5f);
    set_param(engine.get(), "saw_gain", 0.3f);
    std::cout << "Playing Middle C (C4) for 2 seconds..." << std::endl;
    engine_note_on_name(engine.get(), "C4", 0.8f);
    std::this_thread::sleep_for(std::chrono::seconds(2));
    engine_note_off_name(engine.get(), "C4");

    engine_stop(engine.get());
    
    std::cout << "\n--- Oscillator Integrity Test Completed. Engine destroyed via RAII. ---" << std::endl;
    return 0;
}
