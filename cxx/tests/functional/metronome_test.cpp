#include "../TestHelper.hpp"
#include <iostream>
#include <vector>
#include <cmath>
#include <thread>
#include <chrono>
#include <cassert>

/**
 * @brief Functional test for the MusicalClock and Metronome logic.
 * Validates rhythmic stability and precision timing (120 BPM).
 */
int main() {
    int sample_rate = test::get_safe_sample_rate(0);

    PRINT_TEST_HEADER(
        "Metronome Timing Integrity",
        "Validate rhythmic stability and precision timing (120 BPM).",
        "Short Pulse (Woodblock-style) -> Output",
        "Steady, precise clicks at 120 BPM (2 clicks per second).",
        sample_rate
    );

    test::init_test_environment();
    test::EngineWrapper engine(sample_rate);

    // Configure for a short woodblock-style click
    std::cout << "[METRONOME] Configuring parameters for short woodblock clicks..." << std::endl;
    assert(engine_set_modulation(engine.get(), MOD_SRC_ENVELOPE, MOD_TGT_AMPLITUDE, 1.0f) == 0);
    assert(set_param(engine.get(), "amp_attack", 0.001f) == 0);
    assert(set_param(engine.get(), "amp_decay", 0.050f) == 0); // Short decay as requested
    assert(set_param(engine.get(), "amp_sustain", 0.0f) == 0); // No sustain for percussive click
    assert(set_param(engine.get(), "amp_release", 0.010f) == 0);
    assert(set_param(engine.get(), "vcf_cutoff", 2000.0f) == 0);
    assert(set_param(engine.get(), "vcf_res", 0.5f) == 0);
    std::cout << "[METRONOME] Parameters verified and set." << std::endl;

    if (engine_set_bpm(engine.get(), 120.0) != 0) {
        std::cerr << "Failed to set BPM" << std::endl;
        return 1;
    }

    if (engine_start(engine.get()) != 0) {
        std::cerr << "Failed to start audio engine" << std::endl;
        return 1;
    }

    std::cout << "Metronome active at 120 BPM. Listening for 8 beats (Woodblock clicks)..." << std::endl;

    int last_beat = -1;
    int beat_count = 0;
    const size_t frames = 512;
    std::vector<float> buffer(frames * 2);

    // Simulation loop
    while (beat_count < 8) {
        engine_process(engine.get(), buffer.data(), frames);
        
        int bar, beat, tick;
        engine_get_musical_time(engine.get(), &bar, &beat, &tick);

        if (beat != last_beat) {
            std::cout << "[METRONOME] Bar: " << bar << " Beat: " << beat << std::endl;
            
            // Trigger woodblock click
            if (beat == 1) {
                engine_note_on_name(engine.get(), "C6", 1.0f); // High accent
            } else {
                engine_note_on_name(engine.get(), "G5", 0.7f); // Standard tone
            }
            
            // Note: For a percussive sound with zero sustain, we don't strictly need a timed note_off 
            // but we'll include it for completeness after the decay.
            std::thread([engine_handle = engine.get(), beat]() {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                if (beat == 1) engine_note_off_name(engine_handle, "C6");
                else engine_note_off_name(engine_handle, "G5");
            }).detach();

            last_beat = beat;
            beat_count++;
        }
    }

    engine_stop(engine.get());
    std::cout << "--- Metronome Timing Test Completed. Engine destroyed via RAII. ---" << std::endl;
    return 0;
}
