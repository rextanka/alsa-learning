#include <iostream>
#include <cassert>
#include <cmath>
#include <algorithm>
#include "../TestHelper.hpp"

/**
 * @file Phase10Tests.cpp
 * @brief Functional verification of BPM, Clock, and note name mapping via Bridge API.
 */ 

void test_musical_logic(int sample_rate) {
    std::cout << "--- Testing Musical Logic (Phase 10) ---" << std::endl;
    
    test::EngineWrapper engine(sample_rate);
    
    // Patch a basic default route
    engine_set_modulation(engine.get(), MOD_SRC_ENVELOPE, MOD_TGT_AMPLITUDE, 0.0f);
    
    // Audit modulation report
    char report[256];
    engine_get_modulation_report(engine.get(), report, sizeof(report));
    std::cout << "[Phase10] Modulation State:\n" << report << std::endl;

    // Test BPM and Clock
    engine_set_bpm(engine.get(), 120.0);
    assert(std::abs(engine_get_bpm(engine.get()) - 120.0) < 0.001);
    
    int bar, beat, tick;
    engine_get_musical_time(engine.get(), &bar, &beat, &tick);
    std::cout << "Initial Time: " << bar << ":" << beat << ":" << tick << std::endl;
    assert(bar == 1 && beat == 1 && tick == 0);
    
    // Process exactly 2 beats.
    // 120 BPM = 2 beats per second.
    // 2 beats = 1.0 second.
    // Process exactly 'sample_rate' frames.
    float dummy_buffer[512 * 2];
    int samples_to_process = sample_rate;
    int processed = 0;
    while (processed < samples_to_process) {
        int to_process = std::min(512, samples_to_process - processed);
        engine_process(engine.get(), dummy_buffer, to_process);
        processed += to_process;
    }
    
    engine_get_musical_time(engine.get(), &bar, &beat, &tick);
    std::cout << "Time after exactly " << processed << " samples: " << bar << ":" << beat << ":" << tick << std::endl;
    
    // 2 beats at 4/4 = 1:3:0
    assert(bar == 1 && beat == 3 && tick == 0);
    
    // Test Playing by Name
    std::cout << "Playing 'C4'..." << std::endl;
    assert(engine_note_on_name(engine.get(), "C4", 0.8f) == 0);
    
    std::cout << "Playing 'A#2'..." << std::endl;
    assert(engine_note_on_name(engine.get(), "A#2", 0.8f) == 0);

    // Test invalid note
    std::cout << "Testing invalid note 'XYZ'..." << std::endl;
    assert(engine_note_on_name(engine.get(), "XYZ", 0.8f) != 0);
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
