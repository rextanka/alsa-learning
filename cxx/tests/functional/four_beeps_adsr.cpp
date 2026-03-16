/**
 * @file four_beeps_adsr.cpp
 * @brief Plays 4 beeps, 1 second apart, descending in pitch to verify ADSR.
 *
 * Chain: COMPOSITE_GENERATOR -> ADSR_ENVELOPE -> VCA (Phase 15)
 * Waveform: Triangle — clean for ADSR articulation audit.
 */

#include "../TestHelper.hpp"
#include <iostream>
#include <vector>
#include <thread>
#include <chrono>

int main() {
    test::init_test_environment();
    int sample_rate = test::get_safe_sample_rate(0);

    PRINT_TEST_HEADER(
        "Validate ADSR envelope articulation and voice lifecycle.",
        "Triangle Oscillator -> ADSR_ENVELOPE -> VCA -> Output.",
        "Four distinct clean beeps with audible attack/decay and no clicks.",
        "4 distinct ADSR-shaped tones (D3, G3, B3, E4) matching guitar strings 4-1.",
        sample_rate
    );

    test::EngineWrapper engine(sample_rate);

    // Phase 15 chain
    engine_add_module(engine.get(), "COMPOSITE_GENERATOR", "VCO");
    engine_add_module(engine.get(), "ADSR_ENVELOPE",       "ENV");
    engine_add_module(engine.get(), "VCA",                 "VCA");
    engine_connect_ports(engine.get(), "ENV", "envelope_out", "VCA", "gain_cv");
    engine_bake(engine.get());

    // Triangle waveform only
    set_param(engine.get(), "pulse_gain",    0.0f);
    set_param(engine.get(), "sub_gain",      0.0f);
    set_param(engine.get(), "saw_gain",      0.0f);
    set_param(engine.get(), "sine_gain",     0.0f);
    set_param(engine.get(), "triangle_gain", 0.8f);

    // VCF open, no resonance
    set_param(engine.get(), "vcf_cutoff", 20000.0f);
    set_param(engine.get(), "vcf_res",    0.0f);

    // ADSR: 50ms attack / 100ms decay / 80% sustain / 100ms release
    set_param(engine.get(), "amp_attack",  0.050f);
    set_param(engine.get(), "amp_decay",   0.100f);
    set_param(engine.get(), "amp_sustain", 0.8f);
    set_param(engine.get(), "amp_release", 0.100f);

    if (engine_start(engine.get()) != 0) {
        std::cerr << "Failed to start audio engine" << std::endl;
        return 1;
    }

    // Guitar strings 4-1: D3, G3, B3, E4
    const char* notes[] = {"D3", "G3", "B3", "E4"};

    for (int i = 0; i < 4; ++i) {
        std::cout << "[BEEP " << (i + 1) << "] Playing " << notes[i] << "..." << std::endl;

        engine_note_on_name(engine.get(), notes[i], 0.8f);
        std::this_thread::sleep_for(std::chrono::milliseconds(750));
        engine_note_off_name(engine.get(), notes[i]);

        if (i < 3) {
            std::this_thread::sleep_for(std::chrono::milliseconds(750));
        }
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    engine_stop(engine.get());
    std::cout << "--- Test Completed. Engine destroyed via RAII. ---" << std::endl;
    return 0;
}
