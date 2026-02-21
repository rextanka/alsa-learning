#include <iostream>
#include <cassert>
#include <cmath>
#include "include/CInterface.h"

void test_musical_logic() {
    std::cout << "--- Testing Musical Logic (Phase 10) ---" << std::endl;
    
    unsigned int sample_rate = 44100;
    EngineHandle engine = engine_create(sample_rate);
    assert(engine != nullptr);
    
    // Test BPM and Clock
    engine_set_bpm(engine, 120.0);
    assert(std::abs(engine_get_bpm(engine) - 120.0) < 0.001);
    
    int bar, beat, tick;
    engine_get_musical_time(engine, &bar, &beat, &tick);
    std::cout << "Initial Time: " << bar << ":" << beat << ":" << tick << std::endl;
    assert(bar == 1 && beat == 1 && tick == 0);
    
    // Process some frames to advance clock
    // At 120 BPM, 44100 samples is 2 seconds, which is 4 beats (1 bar)
    float dummy_buffer[512];
    for(int i = 0; i < 44100 / 512; ++i) {
        engine_process(engine, dummy_buffer, 512);
    }
    
    engine_get_musical_time(engine, &bar, &beat, &tick);
    std::cout << "Time after ~1 bar: " << bar << ":" << beat << ":" << tick << std::endl;
    
    // Test Playing by Name
    std::cout << "Playing 'C4'..." << std::endl;
    assert(engine_note_on_name(engine, "C4", 0.8f) == 0);
    
    std::cout << "Playing 'A#2'..." << std::endl;
    assert(engine_note_on_name(engine, "A#2", 0.8f) == 0);

    // Test invalid note
    std::cout << "Testing invalid note 'XYZ'..." << std::endl;
    assert(engine_note_on_name(engine, "XYZ", 0.8f) != 0);
    
    engine_destroy(engine);
    std::cout << "--- Phase 10 Tests Passed! ---" << std::endl;
}

int main() {
    try {
        test_musical_logic();
    } catch (const std::exception& e) {
        std::cerr << "Test failed with exception: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
