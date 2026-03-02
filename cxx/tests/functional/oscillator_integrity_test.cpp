/**
 * @file oscillator_integrity_test.cpp
 * @brief Solo test for individual oscillators to verify they can make sound.
 */

#include <iostream>
#include <thread>
#include <chrono>
#include "../../include/CInterface.h"
#include "../TestHelper.hpp"

void run_solo_test(EngineHandle engine, const char* name, const char* label, float val) {
    std::cout << "\n>>> SOLO TEST: " << name << " <<<" << std::endl;
    
    // Reset all gains
    set_param(engine, "pulse_gain", 0.0f);
    set_param(engine, "sub_gain", 0.0f);
    set_param(engine, "saw_gain", 0.0f);
    set_param(engine, "noise_gain", 0.0f);
    
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
    std::cout << "--- Starting Oscillator Integrity Test (Middle C) ---" << std::endl;

    test::init_test_environment();
    
    unsigned int sample_rate = 44100;
    EngineHandle engine = engine_create(sample_rate);
    if (!engine) return 1;

    // Force path clearance
    engine_set_modulation(engine, MOD_SRC_ENVELOPE, MOD_TGT_AMPLITUDE, 1.0f);
    set_param(engine, "amp_sustain", 1.0f);
    set_param(engine, "vcf_cutoff", 5000.0f);

    if (engine_start(engine) != 0) {
        std::cerr << "Failed to start audio engine" << std::endl;
        return 1;
    }

    // Step 1: Main Pulse Solo
    run_solo_test(engine, "Main Pulse (Square)", "pulse_gain", 1.0f);

    // Step 2: Sub Only
    run_solo_test(engine, "Sub Oscillator (Locked)", "sub_gain", 1.0f);

    // Step 3: Sawtooth (if implemented in do_pull)
    run_solo_test(engine, "Sawtooth Oscillator", "saw_gain", 1.0f);

    // Step 4: Combined Mix
    std::cout << "\n>>> SOLO TEST: Combined Mix <<<" << std::endl;
    set_param(engine, "pulse_gain", 1.0f);
    set_param(engine, "sub_gain", 0.5f);
    set_param(engine, "saw_gain", 0.3f);
    std::cout << "Playing Middle C (C4) for 2 seconds..." << std::endl;
    engine_note_on_name(engine, "C4", 0.8f);
    std::this_thread::sleep_for(std::chrono::seconds(2));
    engine_note_off_name(engine, "C4");

    engine_stop(engine);
    engine_destroy(engine);
    
    std::cout << "\n--- Oscillator Integrity Test Completed ---" << std::endl;
    return 0;
}
