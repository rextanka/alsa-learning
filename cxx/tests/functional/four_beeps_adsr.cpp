#include <iostream>
#include <vector>
#include <thread>
#include <chrono>
#include <cassert>
#include "../../include/CInterface.h"
#include "../TestHelper.hpp"

/**
 * @brief Functional test: four_beeps_adsr
 * Plays 4 beeps, 1 second apart, descending in pitch.
 */
int main() {
    std::cout << "--- Starting Functional Test: four_beeps_adsr (48kHz) ---" << std::endl;

    test::init_test_environment();
    
    // Lock to Razer hardware profile sample rate
    unsigned int sample_rate = 48000; 
    EngineHandle engine = engine_create(sample_rate);
    if (!engine) return 1;

    // 1. Configure a 0.5s "Beep" Envelope
    // We use a high sustain (0.8) to ensure the tone is held during the gate.
    set_param(engine, "sub_gain", 0.0f);
    set_param(engine, "saw_gain", 0.8f);
    set_param(engine, "pulse_gain", 0.0f);
    
    // Explicitly set base amplitude to 1.0
    set_param(engine, "amp_base", 1.0f); 

    assert(engine_set_modulation(engine, MOD_SRC_ENVELOPE, MOD_TGT_AMPLITUDE, 1.0f) == 0);
    assert(set_param(engine, "amp_attack", 0.050f) == 0);  // 50ms fade in
    assert(set_param(engine, "amp_decay", 0.100f) == 0);   // 100ms decay
    assert(set_param(engine, "amp_sustain", 0.8f) == 0);   // Hold at 80% volume
    assert(set_param(engine, "amp_release", 0.100f) == 0);  // 100ms fade out

    if (engine_start(engine) != 0) {
        std::cerr << "Failed to start audio engine" << std::endl;
        return 1;
    }

    // 2. Define descending notes (C4, B3, A3, G3)
    const char* notes[] = {"C4", "B3", "A3", "G3"};
    
    for (int i = 0; i < 4; ++i) {
        std::cout << "[BEEP " << (i + 1) << "] Playing " << notes[i] << "..." << std::endl;
        
        // Trigger Note On
        engine_note_on_name(engine, notes[i], 0.8f);
        
        // Hold for 0.5 seconds (User Request)
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        
        // Trigger Note Off
        engine_note_off_name(engine, notes[i]);
        
        // Wait for the remainder of the 1-second interval
        if (i < 3) {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
    }

    std::cout << "--- Test Completed ---" << std::endl;
    engine_destroy(engine);
    return 0;
}
