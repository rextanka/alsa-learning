/**
 * @file metronome_test.cpp
 * @brief Simple validation of MusicalClock and PulseOscillator using the C-Bridge.
 */

#include <iostream>
#include <memory>
#include <thread>
#include <chrono>
#include <cmath>
#include "../../include/CInterface.h"

int main() {
    std::cout << "--- Starting Metronome Validation Test (Bridge-Based) ---" << std::endl;

    unsigned int sample_rate = 44100;
    EngineHandle engine = engine_create(sample_rate);
    if (!engine) return 1;

    engine_set_bpm(engine, 120.0);
    
    // In a real scenario, we'd use a real AudioDriver, but for this functional test 
    // we'll manually pump the engine to simulate time passing and check triggers.
    
    int last_beat = -1;
    int bars_to_run = 2;
    
    std::cout << "Simulating " << bars_to_run << " bars at 120 BPM..." << std::endl;

    float buffer[512];
    int bar, beat, tick;
    
    // 120 BPM, 44100 Hz -> 22050 samples per beat.
    // 2 bars = 8 beats = 176400 samples.
    int total_samples = 176400;
    int processed = 0;
    
    while (processed < total_samples) {
        engine_get_musical_time(engine, &bar, &beat, &tick);
        
        if (beat != last_beat) {
            if (beat == 1) {
                std::cout << "[Beat 1] Triggering C4" << std::endl;
                engine_note_on_name(engine, "C4", 0.8f);
            } else {
                std::cout << "[Beat " << beat << "] Triggering A3" << std::endl;
                engine_note_on_name(engine, "A3", 0.8f);
            }
            last_beat = beat;
        }
        
        engine_process(engine, buffer, 512);
        processed += 512;
    }

    engine_destroy(engine);
    std::cout << "--- Test Completed ---" << std::endl;

    return 0;
}
