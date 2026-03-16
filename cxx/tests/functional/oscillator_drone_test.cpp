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

    // Reset all mixer gains to prevent bleed
    const char* all_gains[] = {
        "pulse_gain", "saw_gain", "sub_gain",
        "sine_gain", "triangle_gain", "wavetable_gain", "noise_gain"
    };
    for (auto g : all_gains) set_param(engine, g, 0.0f);

    set_param(engine, gain_param,      1.0f);
    set_param(engine, "amp_attack",    0.0f);
    set_param(engine, "amp_sustain",   1.0f);

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
        "COMPOSITE_GENERATOR -> ADSR_ENVELOPE -> VCA -> Output",
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

    play_drone(engine.get(), "SINE",     "sine_gain");
    play_drone(engine.get(), "SQUARE",   "pulse_gain");
    play_drone(engine.get(), "SAW",      "saw_gain");
    play_drone(engine.get(), "TRIANGLE", "triangle_gain");
    play_drone(engine.get(), "NOISE",    "noise_gain");

    engine_stop(engine.get());
    std::cout << "--- Test Complete. Engine destroyed via RAII. ---" << std::endl;
    return 0;
}
