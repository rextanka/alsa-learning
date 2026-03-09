/**
 * @file audio_check.cpp
 * @brief Basic audio driver functional check using Bridge API.
 */

#include "../TestHelper.hpp"
#include <iostream>

int main() {
    test::init_test_environment();
    int sample_rate = test::get_safe_sample_rate(0);

    PRINT_TEST_HEADER(
        "Audio Driver Check",
        "Verifies basic audio callback and driver stability",
        "Sine Oscillator -> Output",
        "Continuous sine tone",
        sample_rate
    );

    test::EngineWrapper engine(sample_rate);

    if (engine_start(engine.get()) != 0) {
        std::cerr << "Failed to start engine." << std::endl;
        return 1;
    }

    // Explicitly initialize gain stage as per TEST_DESC.md Tier 1 requirements
    set_param(engine.get(), "sine_gain", 1.0f);

    engine_note_on(engine.get(), 69, 0.5f);

    std::cout << "Playing for 2 seconds..." << std::endl;
    test::wait_while_running(2);

    engine_note_off(engine.get(), 69);
    engine_stop(engine.get());

    std::cout << "--- Done ---" << std::endl;

    return 0;
}
