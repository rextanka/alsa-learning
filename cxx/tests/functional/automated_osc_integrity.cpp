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

/**
 * @brief Helper to verify frequency for a single target.
 */
bool verify_freq(EngineHandle engine, audio::DctProcessor& dct, float target_freq, float sample_rate) {
    std::cout << "[VERIFY] Target: " << target_freq << " Hz" << std::endl;
    
    // 1. Setup engine state
    engine_audiotap_reset(engine);
    
    // Convert frequency to MIDI note for engine_note_on
    // f = 440 * 2^((n-69)/12) -> n = 69 + 12 * log2(f/440)
    int note = static_cast<int>(std::round(69.0 + 12.0 * std::log2(target_freq / 440.0)));
    engine_note_on(engine, note, 1.0f);

    // 2. "Warm up" and fill the tap buffer. 
    // We need to fill at least dct.get_input_size() samples.
    const size_t block_size = 512;
    std::vector<float> dummy_buffer(block_size * 2); // Stereo
    size_t needed = dct.get_input_size();
    
    // Process enough blocks to fill the tap
    for (size_t i = 0; i < (needed / block_size) + 4; ++i) {
        engine_process(engine, dummy_buffer.data(), block_size);
    }

    // 3. Capture the most recent samples from the tap matching the DCT input size
    std::vector<float> captured(dct.get_input_size());
    engine_audiotap_read(engine, captured.data(), dct.get_input_size());

    // 4. Run DCT analysis (with zero-padding inside)
    std::vector<float> magnitudes(dct.get_dct_size());
    dct.process(captured, magnitudes);

    // 5. Detect pitch with sub-bin accuracy
    float detected = audio::PitchDetector::detect(magnitudes, sample_rate);
    
    float error = std::abs(detected - target_freq);
    float error_percent = (target_freq > 0) ? (error / target_freq) * 100.0f : 0.0f;

    std::cout << "  Detected: " << std::fixed << std::setprecision(3) << detected << " Hz"
              << " | Error: " << std::setprecision(3) << error << " Hz (" 
              << std::setprecision(2) << error_percent << "%)" << std::endl;

    // Clean up note
    engine_note_off(engine, note);

    // Deviation limit: 0.5%
    if (error_percent > 0.5f) {
        std::cerr << "  [FAIL] Frequency deviation " << error_percent << "% exceeds 0.5% limit!" << std::endl;
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
        "Detected frequencies within 0.5% of MIDI targets.",
        sample_rate
    );

    // 1. Initialize Engine via Bridge (Golden Lifecycle)
    test::EngineWrapper engine(sample_rate);
    
    // 2. Setup Analysis Components (Helper classes)
    // input_size = 16384, dct_size = 32768 (double resolution via zero-padding)
    audio::DctProcessor dct(16384, 32768);

    // 3. Run Test Suite for standard MIDI pitches
    bool all_passed = true;
    
    // Explicitly set sine gain as primary oscillator
    set_param(engine.get(), "sine_gain", 1.0f);
    
    // Testing A2 (110Hz), A3 (220Hz), A4 (440Hz)
    all_passed &= verify_freq(engine.get(), dct, 110.00f, (float)sample_rate);
    all_passed &= verify_freq(engine.get(), dct, 220.00f, (float)sample_rate);
    all_passed &= verify_freq(engine.get(), dct, 440.00f, (float)sample_rate);
    
    // Testing a high note to check stability
    all_passed &= verify_freq(engine.get(), dct, 880.00f, (float)sample_rate);

    if (!all_passed) {
        std::cerr << "FAILED: One or more oscillator frequency tests failed." << std::endl;
        return 1;
    }

    std::cout << "SUCCESS: All Pitch Integrity Tests PASSED" << std::endl;
    return 0;
}
