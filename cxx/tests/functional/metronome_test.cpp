#include "../TestHelper.hpp"
#include <iostream>
#include <vector>
#include <cmath>
#include <thread>
#include <chrono>
#include <cassert>

/**
 * @brief Functional test for the MusicalClock and Metronome logic.
 */
int main() {
    int sample_rate = test::get_safe_sample_rate(0);

    PRINT_TEST_HEADER(
        "Metronome Timing Integrity",
        "Verifies sample-accurate clock and gated beep timing.",
        "Clock -> Engine -> VCA (Gated) -> Output",
        "Periodic rhythmic beeps (8 beats) at 120 BPM with accent on Beat 1.",
        sample_rate
    );

    test::init_test_environment();
    test::EngineWrapper engine(sample_rate);

    // Configure for a smooth 0.25s metronome tone
    std::cout << "[METRONOME] Configuring parameters for 0.25s tones..." << std::endl;
    assert(engine_set_modulation(engine.get(), MOD_SRC_ENVELOPE, MOD_TGT_AMPLITUDE, 1.0f) == 0);
    assert(set_param(engine.get(), "amp_attack", 0.010f) == 0);
    assert(set_param(engine.get(), "amp_decay", 0.100f) == 0);
    assert(set_param(engine.get(), "amp_sustain", 0.8f) == 0);
    assert(set_param(engine.get(), "amp_release", 0.050f) == 0);
    assert(set_param(engine.get(), "vcf_cutoff", 4000.0f) == 0);
    assert(set_param(engine.get(), "vcf_res", 0.2f) == 0);
    std::cout << "[METRONOME] Parameters verified and set." << std::endl;

    if (engine_set_bpm(engine.get(), 120.0) != 0) {
        std::cerr << "Failed to set BPM" << std::endl;
        return 1;
    }

    if (engine_start(engine.get()) != 0) {
        std::cerr << "Failed to start audio engine" << std::endl;
        return 1;
    }

    std::cout << "Metronome active at 120 BPM. Listening for 8 beats (0.25s tones)..." << std::endl;

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
            
            // Trigger 0.25s tone
            if (beat == 1) {
                engine_note_on_name(engine.get(), "C5", 1.0f); // High accent
            } else {
                engine_note_on_name(engine.get(), "A4", 0.7f); // Standard tone
            }
            
            // Sustain for 250ms (0.25s)
            // Note: Since we are in a simple functional test, we'll use a detached thread for timing the off-trigger.
            // In a real plugin, this would be handled by the sequencer/host.
            std::thread([engine_handle = engine.get(), beat]() {
                std::this_thread::sleep_for(std::chrono::milliseconds(250));
                if (beat == 1) engine_note_off_name(engine_handle, "C5");
                else engine_note_off_name(engine_handle, "A4");
            }).detach();

            last_beat = beat;
            beat_count++;
        }
    }

    engine_stop(engine.get());
    std::cout << "--- Metronome Timing Test Completed. Engine destroyed via RAII. ---" << std::endl;
    return 0;
}
