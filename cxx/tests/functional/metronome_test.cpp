#include "../TestHelper.hpp"
#include <iostream>
#include <vector>
#include <cmath>
#include <thread>
#include <chrono>
#include <cassert>
#include <string>

/**
 * @brief Functional test for the MusicalClock and Metronome logic.
 * Validates rhythmic stability and precision timing.
 *  
 * Usage: ./metronome_test [bpm] [bars] [sig] [--analyze]
 */
int main(int argc, char** argv) {
    // 1. Positional CLI Parsing
    double bpm = 120.0;
    if (argc > 1 && std::string(argv[1]) != "" && std::string(argv[1]) != "--analyze") {
        bpm = std::stod(argv[1]);
    }

    int total_bars = 2;
    if (argc > 2 && std::string(argv[2]) != "" && std::string(argv[2]) != "--analyze") {
        total_bars = std::stoi(argv[2]);
    }

    int beats_per_bar = 4;
    if (argc > 3 && std::string(argv[3]) != "" && std::string(argv[3]) != "--analyze") {
        beats_per_bar = std::stoi(argv[3]);
    }

    bool analyze_mode = false;
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--analyze") {
            analyze_mode = true;
            break;
        }
    }

    int sample_rate = test::get_safe_sample_rate(0);

    if (!analyze_mode) {
        PRINT_TEST_HEADER(
            "Metronome Timing Integrity",
            "Validate rhythmic stability and precision timing.",
            "Short Pulse (Woodblock-style) -> Output",
            "Steady, precise clicks at requested BPM.",
            sample_rate
        );
    }

    test::init_test_environment();
    test::EngineWrapper engine(sample_rate);

    // 2. DSP Audibility & Quality Optimization
    if (!analyze_mode) std::cout << "[METRONOME] Configuring parameters for woodblock clicks..." << std::endl;
    // Using engine_connect_mod as per TEST_DESC.md Tier 2 protocol
    assert(engine_connect_mod(engine.get(), MOD_SRC_ENVELOPE, ALL_VOICES, MOD_TGT_AMPLITUDE, 1.0f) == 0);
    assert(set_param(engine.get(), "amp_attack", 0.001f) == 0);
    assert(set_param(engine.get(), "amp_decay", 0.050f) == 0);
    assert(set_param(engine.get(), "amp_sustain", 0.0f) == 0);
    assert(set_param(engine.get(), "amp_release", 0.010f) == 0);
    assert(set_param(engine.get(), "vcf_cutoff", 1.0f) == 0); // Fully Open for audibility
    assert(set_param(engine.get(), "vcf_res", 0.1f) == 0);
    assert(set_param(engine.get(), "pulse_width", 0.5f) == 0); // Square wave
    assert(set_param(engine.get(), "pulse_gain", 0.7f) == 0);
    if (!analyze_mode) std::cout << "[METRONOME] Parameters verified and set." << std::endl;

    // 3. Clock & Meter Integration
    if (engine_set_bpm(engine.get(), bpm) != 0) {
        std::cerr << "Failed to set BPM" << std::endl;
        return 1;
    }

    if (engine_set_meter(engine.get(), beats_per_bar) != 0) {
        std::cerr << "Failed to set meter" << std::endl;
        return 1;
    }

    if (engine_start(engine.get()) != 0) {
        std::cerr << "Failed to start audio engine" << std::endl;
        return 1;
    }

    if (!analyze_mode) {
        std::cout << "Metronome active at " << bpm << " BPM (" << beats_per_bar << "/4). Listening for " 
                  << total_bars << " bars..." << std::endl;
    }

    int last_beat = -1;
    int beats_processed = 0;
    int total_beats_expected = total_bars * beats_per_bar;

    // 4. Real-Time Polling Loop
    while (beats_processed < total_beats_expected) {
        int bar, beat, tick;
        engine_get_musical_time(engine.get(), &bar, &beat, &tick);

        if (beat != last_beat) {
            auto now = std::chrono::steady_clock::now();
            auto us_timestamp = std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count();

            // Trigger woodblock click (C6 for accent, G5 for standard)
            if (beat == 1) {
                engine_note_on_name(engine.get(), "C6", 1.0f);
            } else {
                engine_note_on_name(engine.get(), "G5", 0.7f);
            }

            // Strict [ANALYSIS] logging for shell script compatibility
            if (analyze_mode) {
                std::cout << "[ANALYSIS] Beat " << (beats_processed + 1) << " Triggered at " << us_timestamp << "us" << std::endl;
            } else {
                std::cout << "[METRONOME] Bar: " << bar << " Beat: " << beat << std::endl;
            }
            
            // Note off after 100ms
            std::thread([engine_handle = engine.get(), beat]() {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                if (beat == 1) engine_note_off_name(engine_handle, "C6");
                else engine_note_off_name(engine_handle, "G5");
            }).detach();

            last_beat = beat;
            beats_processed++;
        }
        
        // 5ms polling interval to avoid CPU hogging while staying responsive
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    // Small buffer at the end to hear the final click decay
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    engine_stop(engine.get());
    if (!analyze_mode) std::cout << "--- Metronome Timing Test Completed. Engine destroyed via RAII. ---" << std::endl;
    return 0;
}
