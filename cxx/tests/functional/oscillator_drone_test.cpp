#include <iostream>
#include <vector>
#include <chrono>
#include <thread>
#include "CInterface.h"

/**
 * oscillator_drone_test.cpp
 * 
 * Purpose: Verifying raw oscillator output at 48kHz with VCA fully open. 
 * This replaces the legacy engine_tests.cpp.
 */

void play_drone(EngineHandle engine, int wave_type, const char* wave_name, float sample_rate) {
    std::cout << "[" << wave_name << "] Playing C4 for 2 seconds..." << std::endl;
    
    // Set oscillator wave type
    set_osc_wavetype(engine, wave_type);
    
    // Configure VCA for drone (bypass decay)
    set_param(engine, "amp_attack", 0.0f);
    set_param(engine, "amp_decay", 0.1f);
    set_param(engine, "amp_sustain", 1.0f);
    set_param(engine, "amp_release", 0.1f);
    
    // Start note
    engine_note_on_name(engine, "C4", 0.8f);
    
    // Wait 2 seconds for hardware output
    std::this_thread::sleep_for(std::chrono::seconds(2));
    
    // Stop note
    engine_note_off_name(engine, "C4");
    
    // Brief wait for release
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
}

int main() {
    const float sample_rate = 48000.0f;
    
    std::cout << "--- Starting Functional Test: oscillator_drone_test ---" << std::endl;
    std::cout << "Purpose: Verifying raw oscillator output at 48kHz with VCA fully open." << std::endl;
    std::cout << "This replaces the legacy engine_tests.cpp." << std::endl;

    EngineHandle engine = engine_create(static_cast<unsigned int>(sample_rate));
    if (!engine) {
        std::cerr << "Failed to create engine" << std::endl;
        return 1;
    }

    if (engine_start(engine) != 0) {
        std::cerr << "Failed to start engine" << std::endl;
        engine_destroy(engine);
        return 1;
    }

    // Test each wave type
    play_drone(engine, WAVE_SINE, "SINE", sample_rate);
    play_drone(engine, WAVE_SQUARE, "SQUARE", sample_rate);
    play_drone(engine, WAVE_SAW, "SAW", sample_rate);
    play_drone(engine, WAVE_TRIANGLE, "TRIANGLE", sample_rate);

    engine_stop(engine);
    engine_destroy(engine);

    std::cout << "--- Test Complete ---" << std::endl;

    return 0;
}
