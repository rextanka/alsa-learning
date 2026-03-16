/**
 * @file metronome_test.cpp
 * @brief Functional test for MusicalClock and Metronome logic.
 *
 * Chain: COMPOSITE_GENERATOR -> ADSR_ENVELOPE -> VCA (Phase 15)
 * Woodblock-style clicks: very fast attack/decay, square wave (pulse).
 *
 * Usage: ./metronome_test [bpm] [bars] [sig] [--analyze]
 */

#include "../TestHelper.hpp"
#include <iostream>
#include <vector>
#include <cmath>
#include <thread>
#include <chrono>
#include <cassert>
#include <string>

int main(int argc, char** argv) {
    double bpm = 120.0;
    if (argc > 1 && std::string(argv[1]) != "--analyze")
        bpm = std::stod(argv[1]);

    int total_bars = 2;
    if (argc > 2 && std::string(argv[2]) != "--analyze")
        total_bars = std::stoi(argv[2]);

    int beats_per_bar = 4;
    if (argc > 3 && std::string(argv[3]) != "--analyze")
        beats_per_bar = std::stoi(argv[3]);

    bool analyze_mode = false;
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--analyze") { analyze_mode = true; break; }
    }

    int sample_rate = test::get_safe_sample_rate(0);

    if (!analyze_mode) {
        PRINT_TEST_HEADER(
            "Metronome Timing Integrity",
            "Validate rhythmic stability and precision timing.",
            "COMPOSITE_GENERATOR -> ADSR_ENVELOPE -> VCA -> Output",
            "Steady, precise clicks at requested BPM.",
            sample_rate
        );
    }

    test::init_test_environment();
    test::EngineWrapper engine(sample_rate);

    // Phase 15 chain
    engine_add_module(engine.get(), "COMPOSITE_GENERATOR", "VCO");
    engine_add_module(engine.get(), "ADSR_ENVELOPE",       "ENV");
    engine_add_module(engine.get(), "VCA",                 "VCA");
    engine_connect_ports(engine.get(), "ENV", "envelope_out", "VCA", "gain_cv");
    engine_bake(engine.get());

    // Woodblock parameters: fast decay, square wave, cutoff wide open
    if (!analyze_mode) std::cout << "[METRONOME] Configuring woodblock clicks..." << std::endl;
    set_param(engine.get(), "amp_attack",  0.001f);
    set_param(engine.get(), "amp_decay",   0.050f);
    set_param(engine.get(), "amp_sustain", 0.0f);
    set_param(engine.get(), "amp_release", 0.010f);
    set_param(engine.get(), "vcf_cutoff",  20000.0f);
    set_param(engine.get(), "vcf_res",     0.1f);
    set_param(engine.get(), "pulse_width", 0.5f);
    set_param(engine.get(), "pulse_gain",  0.7f);
    if (!analyze_mode) std::cout << "[METRONOME] Parameters set." << std::endl;

    if (engine_set_bpm(engine.get(), bpm) != 0) {
        std::cerr << "Failed to set BPM" << std::endl; return 1;
    }
    if (engine_set_meter(engine.get(), beats_per_bar) != 0) {
        std::cerr << "Failed to set meter" << std::endl; return 1;
    }
    if (engine_start(engine.get()) != 0) {
        std::cerr << "Failed to start audio engine" << std::endl; return 1;
    }

    if (!analyze_mode)
        std::cout << "Metronome active at " << bpm << " BPM (" << beats_per_bar
                  << "/4). Listening for " << total_bars << " bars..." << std::endl;

    int last_beat       = -1;
    int beats_processed = 0;
    int total_beats     = total_bars * beats_per_bar;

    while (beats_processed < total_beats) {
        int bar, beat, tick;
        engine_get_musical_time(engine.get(), &bar, &beat, &tick);

        if (beat != last_beat) {
            auto us = std::chrono::duration_cast<std::chrono::microseconds>(
                          std::chrono::steady_clock::now().time_since_epoch()).count();

            if (beat == 1)
                engine_note_on_name(engine.get(), "C6", 1.0f);
            else
                engine_note_on_name(engine.get(), "G5", 0.7f);

            if (analyze_mode)
                std::cout << "[ANALYSIS] Beat " << (beats_processed + 1)
                          << " Triggered at " << us << "us" << std::endl;
            else
                std::cout << "[METRONOME] Bar: " << bar << " Beat: " << beat << std::endl;

            std::thread([h = engine.get(), beat]() {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                if (beat == 1) engine_note_off_name(h, "C6");
                else           engine_note_off_name(h, "G5");
            }).detach();

            last_beat = beat;
            ++beats_processed;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    engine_stop(engine.get());
    if (!analyze_mode)
        std::cout << "--- Metronome Timing Test Completed. Engine destroyed via RAII. ---" << std::endl;
    return 0;
}
