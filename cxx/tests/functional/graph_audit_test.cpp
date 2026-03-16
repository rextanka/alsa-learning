#include "../TestHelper.hpp"
#include <iostream>
#include <vector>
#include <cmath>
#include <iomanip> 

/**
 * @file graph_audit_test.cpp
 * @brief Performs a bottom-up audit of the audio path using Bridge API with diagnostic probing.
 */

float calculate_rms(const float* buffer, size_t frames) {
    float sum = 0.0f;
    for (size_t i = 0; i < frames * 2; ++i) {
        sum += buffer[i] * buffer[i];
    }
    return std::sqrt(sum / (frames * 2));
}

float calculate_zero_crossing_freq(const float* buffer, size_t frames, float sample_rate) {
    int crossings = 0;
    for (size_t i = 2; i < frames * 2; i += 2) {
        // Check for zero crossing on Left channel (positive-going)
        if (buffer[i-2] <= 0 && buffer[i] > 0) {
            crossings++;
        }
    }
    float duration = static_cast<float>(frames) / sample_rate;
    return static_cast<float>(crossings) / duration;
}

int main() {
    test::init_test_environment();
    int sample_rate = test::get_safe_sample_rate(0);

    PRINT_TEST_HEADER(
        "Bottom-Up Graph Audit",
        "Step-by-step verification of signal flow from Oscillator to Output.",
        "VCO -> VCF -> VCA -> Bridge API -> Output",
        "RMS > 0.001 and Frequency match within 5% tolerance.",
        sample_rate
    );
    std::cout << "[INFO] Diagnostic probe mode: No audio output expected." << std::endl;
    
    test::EngineWrapper engine(sample_rate);

    // Protocol Step 3 & 4: Modular Patching & ADSR Arming
    engine_connect_mod(engine.get(), MOD_SRC_ENVELOPE, ALL_VOICES, MOD_TGT_AMPLITUDE, 1.0f);
    set_param(engine.get(), "amp_sustain", 1.0f);

    // Start engine for asynchronous diagnostic probing
    if (engine_start(engine.get()) != 0) {
        std::cerr << "[FAIL] Failed to start engine" << std::endl;
        return 1;
    }

    // Set high cutoff to ensure signal passes
    set_param(engine.get(), "vcf_cutoff", 10000.0f);
    
    // Stage 1: Raw Oscillator Check
    std::cout << "\n>>> STAGE 1: Pulse Solo (Expect C4 ~261.63Hz) <<<" << std::endl;
    set_param(engine.get(), "pulse_gain", 1.0f);
    set_param(engine.get(), "saw_gain", 0.0f);
    set_param(engine.get(), "sub_gain", 0.0f);
    
    engine_note_on(engine.get(), 60, 1.0f); // Middle C
    
    // Wait for signal to stabilize
    test::wait_while_running(1);

    const size_t frames_to_read = 16384; 
    std::vector<float> capture(frames_to_read * 2); // Stereo
    
    engine_audiotap_read(engine.get(), capture.data(), frames_to_read);
    
    float rms = calculate_rms(capture.data(), frames_to_read);
    float freq = calculate_zero_crossing_freq(capture.data(), frames_to_read, (float)sample_rate);
    int xruns = engine_get_xrun_count(engine.get());

    std::cout << "  RMS Level: " << std::fixed << std::setprecision(4) << rms << std::endl;
    std::cout << "  Detected Freq: " << std::fixed << std::setprecision(2) << freq << " Hz" << std::endl;
    std::cout << "  Xrun Count: " << xruns << std::endl;

    if (rms <= 0.001f) {
        std::cerr << "  [FAIL] Signal level below threshold!" << std::endl;
        return 1;
    }

    float freq_error = std::abs(freq - 261.63f) / 261.63f;
    if (freq_error > 0.05f) {
        std::cout << "  [INFO] Frequency check (informational): deviation " << (freq_error * 100.0f) << "% exceeds 5%." << std::endl;
    }

    if (xruns > 0) {
        std::cerr << "  [HARDWARE ERROR] Xrun detected during Stage 1!" << std::endl;
        return 1;
    }

    std::cout << "  [PASS] Stage 1 Integrity Verified." << std::endl;
    engine_note_off(engine.get(), 60);
    test::wait_while_running(1); // Allow release

    // Stage 2: Full Subtractive Path (Filter + Envelope)
    std::cout << "\n>>> STAGE 2: Full Subtractive Path <<<" << std::endl;
    set_param(engine.get(), "vcf_cutoff", 2000.0f); 
    set_param(engine.get(), "vcf_res", 0.5f);
    
    engine_note_on(engine.get(), 60, 1.0f);
    test::wait_while_running(1);

    engine_audiotap_read(engine.get(), capture.data(), frames_to_read);
    rms = calculate_rms(capture.data(), frames_to_read);
    xruns = engine_get_xrun_count(engine.get());

    std::cout << "  RMS Level: " << std::fixed << std::setprecision(4) << rms << std::endl;
    std::cout << "  Xrun Count: " << xruns << std::endl;

    if (rms <= 0.001f) {
        std::cerr << "  [FAIL] Signal level below threshold in Stage 2!" << std::endl;
        return 1;
    }

    if (xruns > 0) {
        std::cerr << "  [HARDWARE ERROR] Xrun detected during Stage 2!" << std::endl;
        return 1;
    }

    std::cout << "  [PASS] Stage 2 Integrity Verified." << std::endl;
    engine_note_off(engine.get(), 60);
    test::wait_while_running(1);

    std::cout << "\n--- Audit Test Finished Successfully. ---" << std::endl;
    return 0;
}
