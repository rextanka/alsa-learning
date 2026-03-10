/**
 * @file automated_osc_integrity.cpp 
 * @brief Tier 2 Functional Test: Automated Pitch Verification.
 * 
 * This test verifies that the oscillators are in tune by analyzing the
 * output of a modular graph using the C Bridge API and AudioTap.
 */

#include "../TestHelper.hpp"
#include "../../src/dsp/analysis/DctProcessor.hpp"
#include "../../src/dsp/analysis/PitchDetector.hpp"
#include <iostream>
#include <vector>
#include <cmath>
#include <iomanip>
#include <thread>
#include <chrono>

/**
 * @brief Helper to verify frequency for a single target.
 */
bool verify_freq(EngineHandle engine, audio::DctProcessor& dct, float target_freq, float sample_rate) {
    std::cout << "[VERIFY] Target: " << target_freq << " Hz" << std::endl;
    
    // 1. Setup engine state and flush previous note
    engine_audiotap_reset(engine);
    
    // Convert frequency to MIDI note for engine_note_on
    // f = 440 * 2^((n-69)/12) -> n = 69 + 12 * log2(f/440)
    int note = static_cast<int>(std::round(69.0 + 12.0 * std::log2(target_freq / 440.0)));
    engine_note_on(engine, note, 1.0f);

    // 2. "Warm up" and fill the tap buffer using the real-time background thread.
    // We need at least 16384 samples (341ms at 48kHz). 0.4s ensures a clean snapshot.
    // Use wait_while_running to keep the thread alive without deadlocking.
    test::wait_while_running(1); // Wait 1 second (rounded up from 0.4s for int-based helper)

    // 3. Capture the most recent samples from the tap matching the DCT input size
    std::vector<float> captured(dct.get_input_size());
    engine_audiotap_read(engine, captured.data(), dct.get_input_size());

    // Audit: Verify analysis buffer is non-zero
    float sum_abs = 0.0f;
    for (float s : captured) sum_abs += std::abs(s);
    if (sum_abs < 1e-6f) {
        std::cerr << "  [FAIL] Analysis buffer is empty (all zeros)!" << std::endl;
        return false;
    }

    // 4. Run DCT analysis (with zero-padding inside)
    std::vector<float> magnitudes(dct.get_dct_size());
    std::cout << "  [DEBUG] Processing DCT (size: " << dct.get_dct_size() << ")..." << std::endl;
    dct.process(captured, magnitudes);
    std::cout << "  [DEBUG] Finished." << std::endl;

    // 5. Detect pitch with sub-bin accuracy
    float detected = audio::PitchDetector::detect(magnitudes, sample_rate);
    
    float error = std::abs(detected - target_freq);
    float error_percent = (target_freq > 0) ? (error / target_freq) * 100.0f : 0.0f;

    std::cout << "  Detected: " << std::fixed << std::setprecision(3) << detected << " Hz"
              << " | Error: " << std::setprecision(3) << error << " Hz (" 
              << std::setprecision(2) << error_percent << "%)" << std::endl;

    // Clean up note and allow release stage to finish
    engine_note_off(engine, note);
    test::wait_while_running(1);

    // Deviation limit: 1.0% (Robust sanity check for mid-range)
    if (error_percent > 1.0f) {
        std::cerr << "  [FAIL] Frequency deviation " << error_percent << "% exceeds 1.0% limit!" << std::endl;
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
        "Engine -> AudioTap -> DCT -> PitchDetector",
        "Detected frequencies within 1.0% of MIDI targets.",
        sample_rate
    );

    // 1. Initialize Engine via Bridge (Golden Lifecycle)
    test::EngineWrapper engine(sample_rate);
    engine_start(engine.get());
    
    // 2. Setup Analysis Components (Helper classes)
    // Stable parameters: 16k window, 32k DCT transform (2x padding)
    audio::DctProcessor dct(16384, 32768);

    // 3. Run Test Suite for mid-to-high range pitches
    bool all_passed = true;
    
    // Explicitly configure for Tier 2 modular architecture
    set_param(engine.get(), "sine_gain", 1.0f);
    engine_set_adsr(engine.get(), 0.001f, 0.01f, 1.0f, 0.01f);
    engine_set_modulation(engine.get(), MOD_SRC_ENVELOPE, MOD_TGT_AMPLITUDE, 1.0f);
    
    // Testing mid-range notes for stability (C4, A4, A5)
    all_passed &= verify_freq(engine.get(), dct, 261.63f, (float)sample_rate);
    all_passed &= verify_freq(engine.get(), dct, 440.00f, (float)sample_rate);
    all_passed &= verify_freq(engine.get(), dct, 880.00f, (float)sample_rate);

    if (!all_passed) {
        std::cerr << "FAILED: One or more oscillator frequency tests failed." << std::endl;
        return 1;
    }

    std::cout << "SUCCESS: All Pitch Integrity Tests PASSED" << std::endl;
    return 0;
}
