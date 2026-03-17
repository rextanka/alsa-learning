/**
 * @file Phase10Tests.cpp
 * @brief Functional verification of BPM, Clock, and note name mapping via Bridge API.
 *
 * Chain: COMPOSITE_GENERATOR -> ADSR_ENVELOPE -> VCA (Phase 15)
 */

#include <iostream>
#include <cassert>
#include <cmath>
#include <thread>
#include <chrono>
#include "../TestHelper.hpp"

static void test_musical_logic(int sample_rate) {
    std::cout << "--- Testing Musical Logic ---" << std::endl;

    test::EngineWrapper engine(sample_rate);

    // Phase 15 chain
    engine_add_module(engine.get(), "COMPOSITE_GENERATOR", "VCO");
    engine_add_module(engine.get(), "ADSR_ENVELOPE",       "ENV");
    engine_add_module(engine.get(), "VCA",                 "VCA");
    engine_connect_ports(engine.get(), "ENV", "envelope_out", "VCA", "gain_cv");
    engine_bake(engine.get());

    set_param(engine.get(), "sine_gain",   1.0f);
    set_param(engine.get(), "amp_attack",  0.01f);
    set_param(engine.get(), "amp_decay",   0.5f);
    set_param(engine.get(), "amp_sustain", 0.2f);

    if (engine_start(engine.get()) != 0)
        throw std::runtime_error("Failed to start audio engine");

    // BPM and Clock
    engine_set_bpm(engine.get(), 120.0);
    assert(std::abs(engine_get_bpm(engine.get()) - 120.0) < 0.001);

    int bar, beat, tick;
    engine_get_musical_time(engine.get(), &bar, &beat, &tick);
    std::cout << "Initial Time: " << bar << ":" << beat << ":" << tick << std::endl;
    assert(bar == 1 && beat == 1 && tick == 0);

    std::cout << "Waiting 1.0s for clock to advance..." << std::endl;
    test::wait_while_running(1);

    engine_get_musical_time(engine.get(), &bar, &beat, &tick);
    std::cout << "Time after 1.0s: " << bar << ":" << beat << ":" << tick << std::endl;
    // 120 BPM = 2 beats/s; 1s -> should be at bar 1 beat >=3
    assert(bar == 1 && beat >= 3);

    // Note-name playback
    std::cout << "Playing 'C4'..." << std::endl;
    assert(engine_note_on_name(engine.get(), "C4", 0.8f) == 0);
    test::wait_while_running(2);
    engine_note_off_name(engine.get(), "C4");

    std::cout << "Playing 'A#3'..." << std::endl;
    assert(engine_note_on_name(engine.get(), "A#3", 0.8f) == 0);
    test::wait_while_running(2);
    engine_note_off_name(engine.get(), "A#3");

    // Invalid note
    std::cout << "Testing invalid note 'XYZ'..." << std::endl;
    assert(engine_note_on_name(engine.get(), "XYZ", 0.8f) != 0);

    engine_stop(engine.get());
}

int main() {
    test::init_test_environment();
    int sample_rate = test::get_safe_sample_rate(0);

    PRINT_TEST_HEADER(
        "Musical Logic Integrity",
        "Verifies BPM, Clock precision, and note name mapping via Bridge API.",
        "COMPOSITE_GENERATOR -> ADSR_ENVELOPE -> VCA -> Clock -> Bridge API",
        "Bar:Beat:Tick progresses correctly; successful note name triggers.",
        sample_rate
    );

    try {
        test_musical_logic(sample_rate);
        std::cout << "--- Musical Logic Tests Passed! Engine destroyed via RAII. ---" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Test failed: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
