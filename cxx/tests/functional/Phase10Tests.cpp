#include <iostream>
#include <cassert>
#include <cmath>
#include <algorithm>
#include "../../include/CInterface.h"

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
    // At 120 BPM, 1 beat is 0.5 seconds.
    // At 44100 Hz, 1 beat is 22050 samples.
    // To get to 1:3:0, we need exactly 2 beats = 44100 samples.
    float dummy_buffer[512];
    int samples_to_process = 44100;
    int processed = 0;
    while (processed < samples_to_process) {
        int to_process = std::min(512, samples_to_process - processed);
        engine_process(engine, dummy_buffer, to_process);
        processed += to_process;
    }
    
    engine_get_musical_time(engine, &bar, &beat, &tick);
    std::cout << "Time after exactly 44,100 samples: " << bar << ":" << beat << ":" << tick << std::endl;
    assert(bar == 1 && beat == 3 && tick == 0);
    
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
