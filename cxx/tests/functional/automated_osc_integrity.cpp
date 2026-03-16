/**
 * @file automated_osc_integrity.cpp
 * @brief Tier 2 Functional Test: Automated Pitch Verification.
 *
 * Verifies oscillators are in tune by analyzing output via the C Bridge API
 * and AudioTap, then running a DCT-based pitch detector.
 *
 * Chain: COMPOSITE_GENERATOR -> ADSR_ENVELOPE -> VCA (Phase 15)
 * Analysis: DctProcessor + PitchDetector (test-local C++ analysis helpers).
 */

#include "../TestHelper.hpp"
#include "../../src/dsp/analysis/DctProcessor.hpp"
#include "../../src/dsp/analysis/PitchDetector.hpp"
#include <iostream>
#include <vector>
#include <cmath>
#include <iomanip>
#include <cassert>
#include <cstring>

static bool verify_freq(EngineHandle engine, audio::DctProcessor& dct,
                        float target_freq, float sample_rate) {
    std::cout << "[VERIFY] Target: " << target_freq << " Hz" << std::endl;

    engine_audiotap_reset(engine);

    int note = static_cast<int>(std::round(69.0 + 12.0 * std::log2(target_freq / 440.0)));
    engine_note_on(engine, note, 1.0f);
    test::wait_while_running(1);

    std::vector<float> captured(dct.get_input_size());
    engine_audiotap_read(engine, captured.data(), dct.get_input_size());

    float sum_abs = 0.0f;
    for (float s : captured) sum_abs += std::abs(s);
    if (sum_abs < 1e-6f) {
        std::cerr << "  [FAIL] Analysis buffer is empty (all zeros)!" << std::endl;
        return false;
    }

    std::vector<float> magnitudes(dct.get_dct_size());
    std::cout << "  [DEBUG] Processing DCT (size: " << dct.get_dct_size() << ")..." << std::endl;
    dct.process(captured, magnitudes);

    float detected    = audio::PitchDetector::detect(magnitudes, sample_rate);
    float error       = std::abs(detected - target_freq);
    float error_pct   = (target_freq > 0.0f) ? (error / target_freq) * 100.0f : 0.0f;

    std::cout << "  Detected: " << std::fixed << std::setprecision(3) << detected << " Hz"
              << "  Error: "    << std::setprecision(3) << error     << " Hz ("
              << std::setprecision(2) << error_pct << "%)" << std::endl;

    engine_note_off(engine, note);
    test::wait_while_running(1);

    if (error_pct > 1.0f) {
        std::cerr << "  [FAIL] Frequency deviation " << error_pct << "% exceeds 1.0% limit!" << std::endl;
        return false;
    }

    std::cout << "  [PASS] Accuracy verified." << std::endl;
    return true;
}

int main() {
    test::init_test_environment();
    int sample_rate = test::get_safe_sample_rate();

    PRINT_TEST_HEADER(
        "Automated Pitch Integrity",
        "Verify oscillators are in tune using the bridge-level AudioTap.",
        "COMPOSITE_GENERATOR -> ADSR_ENVELOPE -> VCA -> AudioTap -> DCT -> PitchDetector",
        "Detected frequencies within 1.0% of MIDI targets.",
        sample_rate
    );

    test::EngineWrapper engine(sample_rate);

    // Phase 15 chain — fast ADSR so the note sustains at full level immediately
    engine_add_module(engine.get(), "COMPOSITE_GENERATOR", "VCO");
    engine_add_module(engine.get(), "ADSR_ENVELOPE",       "ENV");
    engine_add_module(engine.get(), "VCA",                 "VCA");
    engine_connect_ports(engine.get(), "ENV", "envelope_out", "VCA", "gain_cv");
    engine_bake(engine.get());

    set_param(engine.get(), "sine_gain",   1.0f);
    set_param(engine.get(), "amp_attack",  0.001f);
    set_param(engine.get(), "amp_decay",   0.01f);
    set_param(engine.get(), "amp_sustain", 1.0f);
    set_param(engine.get(), "amp_release", 0.01f);

    engine_start(engine.get());

    audio::DctProcessor dct(16384, 32768);

    bool all_passed = true;
    all_passed &= verify_freq(engine.get(), dct, 261.63f, static_cast<float>(sample_rate));
    all_passed &= verify_freq(engine.get(), dct, 440.00f, static_cast<float>(sample_rate));
    all_passed &= verify_freq(engine.get(), dct, 880.00f, static_cast<float>(sample_rate));

    if (!all_passed) {
        std::cerr << "FAILED: One or more oscillator frequency tests failed." << std::endl;
        return 1;
    }

    std::cout << "SUCCESS: All Pitch Integrity Tests PASSED" << std::endl;
    return 0;
}
