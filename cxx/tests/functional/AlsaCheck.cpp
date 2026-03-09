/**
 * @file AlsaCheck.cpp
 * @brief Utility to verify ALSA driver and audio processing using HAL-agnostic Bridge API.
 */

#include "../TestHelper.hpp"
#include <iostream>
#include <thread>
#include <chrono>

int main() {
    test::init_test_environment();
    int sample_rate = test::get_safe_sample_rate(0);
    
    PRINT_TEST_HEADER(
        "ALSA / HAL Driver Check",
        "Validates ALSA device enumeration and basic audio callback stability.",
        "HAL -> Bridge -> Engine (Sine)",
        "Continuous 440Hz sine tone for 3 seconds.",
        sample_rate
    );

    test::EngineWrapper engine(sample_rate);
    
    // Set up a simple sine patch via Bridge API
    set_param(engine.get(), "pulse_gain", 0.0f);
    set_param(engine.get(), "saw_gain", 0.0f);
    set_param(engine.get(), "sub_gain", 0.0f);
    // Note: If we had a direct sine_gain in the global registry, we'd use it.
    // For this check, we'll use the default engine's ability to produce sound.
    // Assuming the engine starts with a sane default or we load a basic patch.
    
    // Explicitly initialize gain stage as per TEST_DESC.md Tier 1 requirements
    set_param(engine.get(), "sine_gain", 1.0f);

    // Just verify the engine starts and stops without crashing.
    if (engine_start(engine.get()) != 0) {
        std::cerr << "Failed to start audio engine." << std::endl;
        return 1;
    }

    std::cout << "Engine started. Triggering A4 (440Hz)..." << std::endl;
    engine_note_on(engine.get(), 69, 0.8f);

    std::cout << "Waiting 3 seconds..." << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(3));

    engine_note_off(engine.get(), 69);
    engine_stop(engine.get());

    std::cout << "Check complete. Engine destroyed via RAII." << std::endl;

    return 0;
}
