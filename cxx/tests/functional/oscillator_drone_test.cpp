/**
 * @file oscillator_drone_test.cpp
 * @brief Raw signal generation verification — 2-second drones for all waveforms.
 *
 * Chain: COMPOSITE_GENERATOR -> ADSR_ENVELOPE -> VCA (Phase 15)
 * Each waveform is isolated via mixer gain parameters.
 */

#include "../TestHelper.hpp"
#include <iostream>
#include <vector>
#include <chrono>
#include <thread>

static void play_drone(EngineHandle engine, const char* wave_name,
                       const char* gain_param) {
    std::cout << "[" << wave_name << "] Playing A4..." << std::endl;
    
    // 1. Isolation: Reset all mixer gains to prevent bleed
    set_param(engine, "pulse_gain",    0.0f);
    set_param(engine, "saw_gain",      0.0f);
    set_param(engine, "sub_gain",      0.0f);
    set_param(engine, "sine_gain",     0.0f);
    set_param(engine, "triangle_gain", 0.0f);
    set_param(engine, "wavetable_gain",0.0f);
    set_param(engine, "noise_gain",    0.0f);

    // 2. Routing: Map the wave type to the correct mixer gain
    if (wave_type == WAVE_SINE) {
        set_param(engine, "sine_gain", 1.0f);
    } else if (wave_type == WAVE_SQUARE) {
        set_param(engine, "pulse_gain", 1.0f);
    } else if (wave_type == WAVE_SAW) {
        set_param(engine, "saw_gain", 1.0f);
    } else if (wave_type == WAVE_TRIANGLE) {
        set_param(engine, "triangle_gain", 1.0f);
    } else if (wave_type == -1) { // NOISE (aperiodic — no wave type)
        set_param(engine, "noise_gain", 1.0f);
    }

    // 3. Global State: Set oscillator wave type and open VCA fully
    if (wave_type >= 0) set_osc_wavetype(engine, wave_type);
    set_param(engine, "amp_attack", 0.0f);
    set_param(engine, "amp_sustain", 1.0f);
    
    // Start note
    engine_note_on_name(engine, "A4", 0.8f);
    std::this_thread::sleep_for(std::chrono::seconds(2));
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
        "Continuous 2-second drone for Sine, Square, Saw, Triangle, and Noise at A4.",
        sample_rate
    );

    test::EngineWrapper engine(sample_rate);

    // Phase 15 chain
    engine_add_module(engine.get(), "COMPOSITE_GENERATOR", "VCO");
    engine_add_module(engine.get(), "ADSR_ENVELOPE",       "ENV");
    engine_add_module(engine.get(), "VCA",                 "VCA");
    engine_connect_ports(engine.get(), "ENV", "envelope_out", "VCA", "gain_cv");
    engine_bake(engine.get());

    if (engine_start(engine.get()) != 0) {
        std::cerr << "Failed to start engine" << std::endl;
        return 1;
    }

    // Test each wave type
    play_drone(engine.get(), WAVE_SINE,     "SINE",     sample_rate);
    play_drone(engine.get(), WAVE_SQUARE,   "SQUARE",   sample_rate);
    play_drone(engine.get(), WAVE_SAW,      "SAW",      sample_rate);
    play_drone(engine.get(), WAVE_TRIANGLE, "TRIANGLE", sample_rate);
    play_drone(engine.get(), -1,            "NOISE",    sample_rate);

    engine_stop(engine.get());
    std::cout << "--- Test Complete. Engine destroyed via RAII. ---" << std::endl;
    return 0;
}
