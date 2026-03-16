/**
 * @file audio_check.cpp
 * @brief Basic audio driver functional check using Bridge API.
 *
 * Chain: COMPOSITE_GENERATOR -> ADSR_ENVELOPE -> VCA (Phase 15)
 * Plays a 2-second sine tone to verify driver and callback stability.
 */

#include "../TestHelper.hpp"
#include <iostream>

int main() {
    test::init_test_environment();
    int sample_rate = test::get_safe_sample_rate(0);

    PRINT_TEST_HEADER(
        "Audio Driver Check",
        "Verifies basic audio callback and driver stability.",
        "COMPOSITE_GENERATOR -> ADSR_ENVELOPE -> VCA -> Output",
        "Continuous 2-second sine tone at A4.",
        sample_rate
    );

    test::EngineWrapper engine(sample_rate);

    // Phase 15 chain
    engine_add_module(engine.get(), "COMPOSITE_GENERATOR", "VCO");
    engine_add_module(engine.get(), "ADSR_ENVELOPE",       "ENV");
    engine_add_module(engine.get(), "VCA",                 "VCA");
    engine_connect_ports(engine.get(), "ENV", "envelope_out", "VCA", "gain_cv");
    engine_bake(engine.get());

    set_param(engine.get(), "sine_gain",  1.0f);
    set_param(engine.get(), "amp_attack", 0.01f);
    set_param(engine.get(), "amp_sustain", 1.0f);

    if (engine_start(engine.get()) != 0) {
        std::cerr << "Failed to start engine." << std::endl;
        return 1;
    }

    engine_note_on(engine.get(), 69, 0.5f);

    std::cout << "Playing A4 for 2 seconds..." << std::endl;
    test::wait_while_running(2);

    engine_note_off(engine.get(), 69);
    engine_stop(engine.get());

    std::cout << "--- Done ---" << std::endl;
    return 0;
}
