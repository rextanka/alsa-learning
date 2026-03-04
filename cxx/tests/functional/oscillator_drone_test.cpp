#include "../TestHelper.hpp"
#include <iostream>
#include <vector>
#include <chrono>
#include <thread>

/**
 * oscillator_drone_test.cpp
 * 
 * Purpose: Verifying raw signal generation for all wave types (2s drones).
 * This test verifies raw oscillator output by creating a 2-second drone for 
 * Sine, Square, Saw, and Triangle waves with the VCA fully open.
 */

void play_drone(EngineHandle engine, int wave_type, const char* wave_name, int sample_rate) {
    std::cout << "[" << wave_name << "] Playing A4..." << std::endl;
    
    // Set oscillator wave type
    set_osc_wavetype(engine, wave_type);
    
    // Open VCA fully
    set_param(engine, "amp_attack", 0.0f);
    set_param(engine, "amp_sustain", 1.0f);
    
    // Start note
    engine_note_on_name(engine, "A4", 0.8f);
    
    // Simulate 2 seconds of real-time wait for hardware output
    std::this_thread::sleep_for(std::chrono::seconds(2));
    
    // Stop note
    engine_note_off_name(engine, "A4");
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
}

int main() {
    int sample_rate = test::get_safe_sample_rate(0);

    PRINT_TEST_HEADER(
        "Oscillator Drone Integrity",
        "Verifying raw signal generation for all wave types (2s drones).",
        "VCO -> VCA -> Output",
        "Continuous 2-second drone for Sine, Square, Saw, and Triangle at A4.",
        sample_rate
    );

    test::EngineWrapper engine(sample_rate);

    if (engine_start(engine.get()) != 0) {
        std::cerr << "Failed to start engine" << std::endl;
        return 1;
    }

    // Test each wave type
    play_drone(engine.get(), WAVE_SINE, "SINE", sample_rate);
    play_drone(engine.get(), WAVE_SQUARE, "SQUARE", sample_rate);
    play_drone(engine.get(), WAVE_SAW, "SAW", sample_rate);
    play_drone(engine.get(), WAVE_TRIANGLE, "TRIANGLE", sample_rate);

    engine_stop(engine.get());

    std::cout << "--- Test Complete. Engine destroyed via RAII. ---" << std::endl;

    return 0;
}
