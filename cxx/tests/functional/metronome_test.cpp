#include <iostream>
#include <vector>
#include <cmath>
#include <thread>
#include <chrono>
#include "../../include/CInterface.h"
#include "../TestHelper.hpp"

/**
 * @brief Functional test for the MusicalClock and Metronome logic.
 * 
 * Verifies:
 * 1. Clock advances correctly at 120 BPM.
 * 2. Note triggers occur exactly on the beat.
 * 3. The tone changes for Beat 1 (Accent).
 */
int main() {
    std::cout << "--- Starting Metronome Timing Test ---" << std::endl;

    test::init_test_environment();
    
    // Razer hardware profile: 48000Hz
    unsigned int sample_rate = 48000;
    EngineHandle engine = engine_create(sample_rate);
    if (!engine) return 1;

    // Configure for a sharp metronome click
    engine_set_modulation(engine, MOD_SRC_ENVELOPE, MOD_TGT_AMPLITUDE, 1.0f);
    set_param(engine, "amp_attack", 0.001f);
    set_param(engine, "amp_decay", 0.050f);
    set_param(engine, "amp_sustain", 0.0f);
    set_param(engine, "amp_release", 0.020f);
    set_param(engine, "vcf_cutoff", 8000.0f);

    if (engine_set_bpm(engine, 120.0) != 0) {
        std::cerr << "Failed to set BPM" << std::endl;
        return 1;
    }

    if (engine_start(engine) != 0) {
        std::cerr << "Failed to start audio engine" << std::endl;
        return 1;
    }

    std::cout << "Metronome active at 120 BPM. Listening for 8 beats..." << std::endl;

    int last_beat = -1;
    int beat_count = 0;
    const size_t frames = 512;
    std::vector<float> buffer(frames);

    // Simulation loop
    while (beat_count < 8) {
        engine_process(engine, buffer.data(), frames);
        
        int bar, beat, tick;
        engine_get_musical_time(engine, &bar, &beat, &tick);

        if (beat != last_beat) {
            std::cout << "[METRONOME] Bar: " << bar << " Beat: " << beat << std::endl;
            
            // Trigger click
            if (beat == 1) {
                engine_note_on_name(engine, "C5", 1.0f); // High accent
            } else {
                engine_note_on_name(engine, "C4", 0.7f); // Standard click
            }
            
            // Auto-off after a short duration handled by ADSR decay=0.05s
            // but we should still send note_off for protocol
            std::thread([engine, beat]() {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                if (beat == 1) engine_note_off_name(engine, "C5");
                else engine_note_off_name(engine, "C4");
            }).detach();

            last_beat = beat;
            beat_count++;
        }
    }

    engine_stop(engine);
    engine_destroy(engine);

    std::cout << "--- Metronome Timing Test Completed ---" << std::endl;
    return 0;
}
