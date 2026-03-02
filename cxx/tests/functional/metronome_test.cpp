/**
 * @file metronome_test.cpp
 * @brief Real-time audible validation of MusicalClock and PulseOscillator using the C-Bridge.
 */

#include <iostream>
#include <memory>
#include <thread>
#include <chrono>
#include <cmath>
#include "../../include/CInterface.h"
#include "../TestHelper.hpp"

int main() {
    std::cout << "--- Starting Audible Metronome Validation Test ---" << std::endl;

    test::init_test_environment();
    
    unsigned int sample_rate = 44100;
    EngineHandle engine = engine_create(sample_rate);
    if (!engine) return 1;

    // Sustain the Tone (Relax the patches)
    engine_set_modulation(engine, MOD_SRC_ENVELOPE, MOD_TGT_AMPLITUDE, 1.0f);
    
    // Forced Synthesis Overrides (using ID or Label via set_param)
    set_param(engine, "amp_attack", 0.01f);
    set_param(engine, "amp_decay", 1.0f);     // Force 1s decay to prove stability
    set_param(engine, "amp_sustain", 0.8f);   // 80% volume hold
    
    set_param(engine, "vcf_cutoff", 5000.0f); // Wide open
    set_param(engine, "vcf_env_amount", 0.0f); // Kill the kick sweep
    
    set_param(engine, "pulse_width", 0.5f);   // Solid square
    set_param(engine, "pulse_gain", 1.0f);    // Full pulse
    set_param(engine, "sub_gain", 0.5f);      // Sub for body
    
    engine_set_bpm(engine, 120.0);
    
    if (engine_start(engine) != 0) {
        std::cerr << "Failed to start audio engine" << std::endl;
        engine_destroy(engine);
        return 1;
    }

    int last_beat = -1;
    auto start_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::seconds(10);
    
    std::cout << "Playing 120 BPM metronome for 10 seconds..." << std::endl;
    
    while (std::chrono::steady_clock::now() - start_time < duration) {
        int bar, beat, tick;
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
        
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    engine_stop(engine);
    engine_flush_logs(engine);
    engine_destroy(engine);
    
    std::cout << "--- Test Completed ---" << std::endl;

    return 0;
}
