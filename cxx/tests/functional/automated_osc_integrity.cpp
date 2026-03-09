/**
 * @file automated_osc_integrity.cpp
 * @brief Tier 2 Functional Test: Automated Pitch Verification.
 * 
 * This test verifies that the oscillators are in tune by analyzing the
 * output of a modular graph (Oscillator -> AudioTap) using DCT-based 
 * pitch detection.
 */

#include "../TestHelper.hpp"
#include "../../src/dsp/analysis/AudioTap.hpp"
#include "../../src/dsp/analysis/DctProcessor.hpp"
#include "../../src/dsp/analysis/PitchDetector.hpp"
#include "../../src/dsp/oscillator/SineOscillatorProcessor.hpp"
#include <iostream>
#include <vector>
#include <cmath>
#include <iomanip>

using namespace audio;

/**
 * @brief Helper to verify frequency for a single target.
 */
bool verify_freq(SineOscillatorProcessor& osc, AudioTap& tap, DctProcessor& dct, float target_freq, float sample_rate) {
    std::cout << "[VERIFY] Target: " << target_freq << " Hz" << std::endl;
    
    // 1. Setup oscillator
    osc.set_frequency(target_freq);
    tap.reset();

    // 2. "Warm up" and fill the tap buffer. 
    // We need to fill at least dct.get_input_size() samples.
    std::vector<float> block(512);
    size_t needed = dct.get_input_size();
    for (size_t i = 0; i < (needed / 512) + 4; ++i) {
        tap.pull(block);
    }

    // 3. Capture the most recent samples from the tap matching the DCT input size
    std::vector<float> captured(dct.get_input_size());
    tap.read(captured);

    // 4. Run DCT analysis (with zero-padding inside)
    std::vector<float> magnitudes(dct.get_dct_size());
    dct.process(captured, magnitudes);

    // 5. Detect pitch with sub-bin accuracy
    float detected = PitchDetector::detect(magnitudes, sample_rate);
    
    // For DCT-II, we might have a slight offset for Sine waves because it's effectively
    // analyzing a cosine basis. However, with parabolic interpolation and a large window,
    // it should be very close.
    
    float error = std::abs(detected - target_freq);
    float error_percent = (target_freq > 0) ? (error / target_freq) * 100.0f : 0.0f;

    std::cout << "  Detected: " << std::fixed << std::setprecision(3) << detected << " Hz"
              << " | Error: " << std::setprecision(3) << error << " Hz (" 
              << std::setprecision(2) << error_percent << "%)" << std::endl;

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
        "Verify oscillators are in tune using a modular analysis chain.",
        "SineOsc -> AudioTap -> DCT -> PitchDetector",
        "Detected frequencies within 0.5% of MIDI targets.",
        sample_rate
    );

    // 1. Construct the Modular Graph
    // Rule: Every signal chain must begin with a Generator.
    SineOscillatorProcessor osc(sample_rate);
    
    // Rule: AudioTap is a Tee junction.
    // Use a large enough buffer for the analysis capture.
    AudioTap tap(16384);
    tap.add_input(&osc);

    // 2. Setup Analysis Components (Self-contained)
    // input_size = 16384, dct_size = 32768 (double resolution via zero-padding)
    DctProcessor dct(16384, 32768);

    // 3. Run Test Suite for standard MIDI pitches
    bool all_passed = true;
    
    // Testing A2 (110Hz), A3 (220Hz), A4 (440Hz)
    all_passed &= verify_freq(osc, tap, dct, 110.00f, (float)sample_rate);
    all_passed &= verify_freq(osc, tap, dct, 220.00f, (float)sample_rate);
    all_passed &= verify_freq(osc, tap, dct, 440.00f, (float)sample_rate);
    
    // Testing a high note to check stability
    all_passed &= verify_freq(osc, tap, dct, 880.00f, (float)sample_rate);

    if (!all_passed) {
        std::cerr << "FAILED: One or more oscillator frequency tests failed." << std::endl;
        return 1;
    }

    std::cout << "SUCCESS: All Pitch Integrity Tests PASSED" << std::endl;
    return 0;
}
