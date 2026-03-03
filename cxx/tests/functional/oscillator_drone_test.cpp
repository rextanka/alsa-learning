#include <iostream>
#include <vector>
#include <chrono>
#include <thread>
#include "CInterface.h"

/**
 * oscillator_drone_test.cpp
 * 
 * Purpose: Verifying raw signal generation for all wave types (2s drones).
 * This test verifies raw oscillator output by creating a 2-second drone for 
 * Sine, Square, Saw, and Triangle waves with the VCA fully open.
 */

void play_drone(EngineHandle engine, int wave_type, const char* wave_name, float sample_rate) {
    std::cout << "[" << wave_name << "] Playing A4..." << std::endl;
    
    // Set oscillator wave type
    set_osc_wavetype(engine, wave_type);
    
    // Open VCA fully
    set_param(engine, "amp_attack", 0.0f);
    set_param(engine, "amp_sustain", 1.0f);
    
    // Start note
    engine_note_on_name(engine, "A4", 0.8f);
    
    // Process audio for 2 seconds
    const int block_size = 1024;
    std::vector<float> buffer(block_size * 2); // Stereo
    int total_blocks = static_cast<int>((2.0f * sample_rate) / block_size);
    
    for (int i = 0; i < total_blocks; ++i) {
        engine_process(engine, buffer.data(), block_size);
        // In a real functional test with hardware, the driver would be handling the output.
        // Here we simulate the passage of time for the engine.
    }
    
    // Stop note
    engine_note_off_name(engine, "A4");
    
    // Process a bit more to allow for any release (though sustain is 1.0)
    for (int i = 0; i < 10; ++i) {
        engine_process(engine, buffer.data(), block_size);
    }
}

int main() {
    const float sample_rate = 48000.0f;
    
    std::cout << "--- Starting Functional Test: oscillator_drone_test ---" << std::endl;
    std::cout << "Purpose: Verifying raw signal generation for all wave types (2s drones)." << std::endl;

    EngineHandle engine = engine_create(static_cast<unsigned int>(sample_rate));
    if (!engine) {
        std::cerr << "Failed to create engine" << std::endl;
        return 1;
    }

    engine_start(engine);

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
