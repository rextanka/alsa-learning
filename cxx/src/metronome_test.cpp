#include <iostream>
#include <vector>
#include <chrono>
#include <thread>
#include "include/CInterface.h"

int main() {
    std::cout << "--- Metronome Console Test (5 Seconds) ---" << std::endl;
    
    unsigned int sample_rate = 44100;
    EngineHandle engine = engine_create(sample_rate);
    engine_set_bpm(engine, 120.0); // 2 beats per second
    
    int last_beat = -1;
    float dummy_buffer[512];
    
    auto start_time = std::chrono::steady_clock::now();
    auto end_time = start_time + std::chrono::seconds(5);
    
    while (std::chrono::steady_clock::now() < end_time) {
        // Advance engine by one block
        engine_process(engine, dummy_buffer, 512);
        
        int bar, beat, tick;
        engine_get_musical_time(engine, &bar, &beat, &tick);
        
        if (beat != last_beat) {
            std::cout << "TICK (Bar: " << bar << ", Beat: " << beat << ")" << std::endl;
            last_beat = beat;
        }
        
        // Simulating real-time pass - in a real app this is driven by audio callback
        // Here we just loop as fast as we can but the 'musical time' is tied to process calls.
    }
    
    engine_destroy(engine);
    std::cout << "--- Metronome Test Finished ---" << std::endl;
    return 0;
}
