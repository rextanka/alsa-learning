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
 *
 * REFACTORED: Now implements Gain Normalization to prevent overlapping signals.
 */

void play_drone(EngineHandle engine, int wave_type, const char* wave_name, int sample_rate) {
    std::cout << "[" << wave_name << "] Playing A4..." << std::endl;
    
    // 1. Isolation: Reset all mixer gains to prevent bleed
    set_param(engine, "pulse_gain", 0.0f);
    set_param(engine, "saw_gain", 0.0f);
    set_param(engine, "sub_gain", 0.0f);
    set_param(engine, "sine_gain", 0.0f);
    set_param(engine, "triangle_gain", 0.0f);
    
    // 2. Routing: Map the wave type to the correct mixer gain
    if (wave_type == WAVE_SINE) {
        set_param(engine, "sine_gain", 1.0f);
    } else if (wave_type == WAVE_SQUARE) {
        set_param(engine, "pulse_gain", 1.0f);
    } else if (wave_type == WAVE_SAW) {
        set_param(engine, "saw_gain", 1.0f);
    } else if (wave_type == WAVE_TRIANGLE) {
        set_param(engine, "triangle_gain", 1.0f);
    }
    
    // 3. Global State: Set oscillator wave type and open VCA fully
    set_osc_wavetype(engine, wave_type);
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
    test::init_test_environment();
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
