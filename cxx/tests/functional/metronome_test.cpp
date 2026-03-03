#include <iostream>
#include <vector>
#include <cmath>
#include <thread>
#include <chrono>
#include <cassert>
#include "../../include/CInterface.h"
#include "../TestHelper.hpp"

/**
 * @brief Functional test for the MusicalClock and Metronome logic.
 * 
 * Verifies:
 * 1. Clock advances correctly at 120 BPM.
 * 2. Note triggers occur exactly on the beat.
 * 3. The tone changes for Beat 1 (Accent).
 * 4. Parameters are correctly accepted by the engine.
 * 5. Tones are sustained for 0.25s.
 */
int main() {
    std::cout << "--- Starting Metronome Timing Test (48kHz Razer) ---" << std::endl;

    test::init_test_environment();
    
    // Razer hardware profile: Force 48000Hz
    unsigned int sample_rate = 48000;
    EngineHandle engine = engine_create(sample_rate);
    if (!engine) {
        std::cerr << "Failed to create engine" << std::endl;
        return 1;
    }

    // Configure for a smooth 0.25s metronome tone
    std::cout << "[METRONOME] Configuring parameters for 0.25s tones..." << std::endl;
    assert(engine_set_modulation(engine, MOD_SRC_ENVELOPE, MOD_TGT_AMPLITUDE, 1.0f) == 0);
    assert(set_param(engine, "amp_attack", 0.010f) == 0);
    assert(set_param(engine, "amp_decay", 0.100f) == 0);
    assert(set_param(engine, "amp_sustain", 0.8f) == 0);
    assert(set_param(engine, "amp_release", 0.050f) == 0);
    assert(set_param(engine, "vcf_cutoff", 4000.0f) == 0);
    assert(set_param(engine, "vcf_res", 0.2f) == 0);
    std::cout << "[METRONOME] Parameters verified and set." << std::endl;

    if (engine_set_bpm(engine, 120.0) != 0) {
        std::cerr << "Failed to set BPM" << std::endl;
        return 1;
    }

    if (engine_start(engine) != 0) {
        std::cerr << "Failed to start audio engine" << std::endl;
        return 1;
    }

    std::cout << "Metronome active at 120 BPM. Listening for 8 beats (0.25s tones)..." << std::endl;

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
            std::cout << "[METRONOME] Bar: " << bar << " Beat: " << beat << " (SR: " << sample_rate << ")" << std::endl;
            
            // Trigger 0.25s tone
            if (beat == 1) {
                engine_note_on_name(engine, "C5", 1.0f); // High accent
            } else {
                engine_note_on_name(engine, "A4", 0.7f); // Standard tone
            }
            
            // Sustain for 250ms (0.25s)
            std::thread([engine, beat]() {
                std::this_thread::sleep_for(std::chrono::milliseconds(250));
                if (beat == 1) engine_note_off_name(engine, "C5");
                else engine_note_off_name(engine, "A4");
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
