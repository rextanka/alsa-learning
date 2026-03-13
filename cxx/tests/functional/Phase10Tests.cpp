#include <iostream>
#include <cassert>
#include <cmath>
#include <algorithm>
#include <thread>
#include <chrono>
#include "../TestHelper.hpp"

/**
 * @file Phase10Tests.cpp
 * @brief Functional verification of BPM, Clock, and note name mapping via Bridge API.
 */ 

void test_musical_logic(int sample_rate) {
    std::cout << "--- Testing Musical Logic (Phase 10) ---" << std::endl;
    
    test::EngineWrapper engine(sample_rate);
    
    // 1. Enable Source and VCA Modulation for audible verification
    set_param(engine.get(), "sine_gain", 1.0f);
    engine_clear_modulations(engine.get());
    engine_connect_mod(engine.get(), MOD_SRC_ENVELOPE, ALL_VOICES, MOD_TGT_AMPLITUDE, 1.0f);
    
    // Set ADSR for musical articulation
    set_param(engine.get(), "amp_attack", 0.01f);
    set_param(engine.get(), "amp_decay", 0.5f);
    set_param(engine.get(), "amp_sustain", 0.2f);
    
    // Audit modulation report
    char report[256];
    engine_get_modulation_report(engine.get(), report, sizeof(report));
    std::cout << "[Phase10] Modulation State:\n" << report << std::endl;
    assert(strstr(report, "Src: 0 -> Tgt: -1 (Param: 3)") != nullptr);

    // Start engine to allow real-time thread to drive the clock
    if (engine_start(engine.get()) != 0) {
        throw std::runtime_error("Failed to start audio engine");
    }

    // Test BPM and Clock
    engine_set_bpm(engine.get(), 120.0);
    assert(std::abs(engine_get_bpm(engine.get()) - 120.0) < 0.001);
    
    int bar, beat, tick;
    engine_get_musical_time(engine.get(), &bar, &beat, &tick);
    std::cout << "Initial Time: " << bar << ":" << beat << ":" << tick << std::endl;
    assert(bar == 1 && beat == 1 && tick == 0);
    
    // Wait exactly 1.0 second for real-time clock advancement
    // 120 BPM = 2 beats per second. 1.0 second = 2 beats.
    std::cout << "Waiting 1.0s for clock to advance..." << std::endl;
    test::wait_while_running(1);
    
    engine_get_musical_time(engine.get(), &bar, &beat, &tick);
    std::cout << "Time after 1.0s: " << bar << ":" << beat << ":" << tick << std::endl;
    
    // 2 beats at 4/4 = 1:3:0
    // Note: Due to scheduling slop, we check if we've reached at least beat 3.
    assert(bar == 1 && beat >= 3);
    
    // Test Playing by Name (Audible verification)
    std::cout << "Playing 'C4'..." << std::endl;
    assert(engine_note_on_name(engine.get(), "C4", 0.8f) == 0);
    test::wait_while_running(1);
    engine_note_off_name(engine.get(), "C4");
    
    std::cout << "Playing 'A#2'..." << std::endl;
    assert(engine_note_on_name(engine.get(), "A#2", 0.8f) == 0);
    test::wait_while_running(1);
    engine_note_off_name(engine.get(), "A#2");

    // Test invalid note
    std::cout << "Testing invalid note 'XYZ'..." << std::endl;
    assert(engine_note_on_name(engine.get(), "XYZ", 0.8f) != 0);

    engine_stop(engine.get());
}

int main() {
    test::init_test_environment();
    int sample_rate = test::get_safe_sample_rate(0);

    PRINT_TEST_HEADER(
        "Musical Logic Integrity (Phase 10)",
        "Verifies BPM, Clock precision, and note name mapping via Bridge API.",
        "Clock -> Engine -> Musical Logic -> Bridge API",
        "Bar:Beat:Tick progress (1:3:0 after 2 beats); successful note name triggers.",
        sample_rate
    );

    try {
        test_musical_logic(sample_rate);
        std::cout << "--- Phase 10 Tests Passed! Engine destroyed via RAII. ---" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Test failed with exception: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
