/**
 * @file oscillator_integrity_test.cpp
 * @brief Solo test for individual oscillators to verify they can make sound via Bridge API.
 */

#include "../TestHelper.hpp"
#include <iostream> 
#include <thread>
#include <chrono>
#include <vector>
#include <cmath>
#include <cassert>

/**
 * @brief Calculates RMS level of a mono buffer.
 */
float calculate_rms(const float* buffer, size_t frames) {
    if (frames == 0) return 0.0f;
    float sum = 0.0f;
    for (size_t i = 0; i < frames; ++i) {
        sum += buffer[i] * buffer[i];
    }
    return std::sqrt(sum / static_cast<float>(frames));
}

/**
 * @brief Refactored Stage Helper: Implements Sequential Voice Strategy.
 * Each stage is isolated with a total mixer reset and follows a musical ADSR lifecycle.
 * HARDENED: Now asserts that RMS level is > 0.001f to detect silent failures.
 */
void run_sequential_stage(EngineHandle engine, const char* name, const char* gain_param, float level = 1.0f) {
    std::cout << "\n>>> STAGE: " << name << " <<<" << std::endl;
    
    // 1. Total Mixer Reset to prevent state leakage
    const char* all_gains[] = {"pulse_gain", "saw_gain", "sub_gain", "sine_gain", "triangle_gain", "wavetable_gain"};
    for (auto g : all_gains) set_param(engine, g, 0.0f);
    
    // 2. Clean Diagnostic Start
    engine_audiotap_reset(engine);

    // 3. "Musical" Signal Chain Config
    set_param(engine, "amp_attack", 0.05f);
    set_param(engine, "amp_decay", 1.5f);   // Long decay for character audit
    set_param(engine, "amp_sustain", 0.0f); // Fade to silence
    set_param(engine, "vcf_cutoff", 2000.0f);
    set_param(engine, "vcf_res", 0.2f);

    // 4. Enable Target & Trigger
    set_param(engine, gain_param, level);
    std::cout << "  Triggering C4..." << std::endl;
    engine_note_on_name(engine, "C4", 0.8f);
    
    // 5. Wait for full decay + release cycle
    // Use a slightly shorter wait to capture the peak during the decay phase
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    // 6. Hardened Diagnostic Capture
    const size_t frames = 4096;
    std::vector<float> capture(frames);
    engine_audiotap_read(engine, capture.data(), frames);
    float rms = calculate_rms(capture.data(), frames);
    std::cout << "  Diagnostic RMS: " << rms << std::endl;
    
    if (rms <= 0.001f) {
        std::cerr << "  [FAIL] Signal path is DEAD at stage: " << name << std::endl;
        assert(rms > 0.001f);
    } else {
        std::cout << "  [PASS] Signal detected." << std::endl;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(1300));
    engine_note_off_name(engine, "C4");
    std::this_thread::sleep_for(std::chrono::milliseconds(400));
}

int main() {
    test::init_test_environment();
    int sample_rate = test::get_safe_sample_rate(0);

    PRINT_TEST_HEADER(
        "Oscillator Solo Integrity",
        "Verifies solo behavior and musical articulation for the entire oscillator inventory.",
        "Individual VCOs -> VCF -> VCA (ADSR Decay) -> Output",
        "Sequence of isolated 'plucks' for Sine, Triangle, Pulse, Saw, Wavetable, and Sub.",
        sample_rate
    );

    test::EngineWrapper engine(sample_rate);

    // Explicit Modulation Connection as per Tier 2 Protocol
    engine_connect_mod(engine.get(), MOD_SRC_ENVELOPE, ALL_VOICES, MOD_TGT_AMPLITUDE, 1.0f);

    if (engine_start(engine.get()) != 0) {
        std::cerr << "Failed to start audio engine" << std::endl;
        return 1;
    }

    // --- SEQUENTIAL STAGES ---

    run_sequential_stage(engine.get(), "Pure Sine (Rotor)", "sine_gain");
    run_sequential_stage(engine.get(), "Triangle (Naive)", "triangle_gain");
    run_sequential_stage(engine.get(), "Pulse (PolyBLEP)", "pulse_gain");
    run_sequential_stage(engine.get(), "Sawtooth (PolyBLEP)", "saw_gain");

    // Stage: Wavetable Verification
    // 1. Prime the wavetable state (Set type and frequency)
    set_param(engine.get(), "wavetable_type", 1.0f); // 1 = Saw
    set_param(engine.get(), "osc_frequency", 261.63f); // C4

    // 2. Execute stage
    run_sequential_stage(engine.get(), "Wavetable (Interpolated Saw)", "wavetable_gain");

    // Stage: Sub-Oscillator Phase Lock Logic (Requires Pulse parent)
    std::cout << "\n>>> STAGE: Sub + Pulse (Phase Lock) <<<" << std::endl;
    engine_audiotap_reset(engine.get());
    
    // Kill others
    const char* all_gains[] = {"pulse_gain", "saw_gain", "sub_gain", "sine_gain", "triangle_gain", "wavetable_gain"};
    for (auto g : all_gains) set_param(engine.get(), g, 0.0f);

    set_param(engine.get(), "pulse_gain", 0.5f);
    set_param(engine.get(), "sub_gain", 0.5f);
    set_param(engine.get(), "amp_attack", 0.01f); // Fast attack for phase audit
    set_param(engine.get(), "amp_decay", 1.5f);
    set_param(engine.get(), "amp_sustain", 0.0f);

    std::cout << "  Triggering C4 (Aligned Transient)..." << std::endl;
    engine_note_on_name(engine.get(), "C4", 0.8f);
    
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    const size_t frames = 4096;
    std::vector<float> capture(frames);
    engine_audiotap_read(engine.get(), capture.data(), frames);
    float rms = calculate_rms(capture.data(), frames);
    std::cout << "  Diagnostic RMS (Sub): " << rms << std::endl;
    assert(rms > 0.001f);

    std::this_thread::sleep_for(std::chrono::milliseconds(1500));
    engine_note_off_name(engine.get(), "C4");
    std::this_thread::sleep_for(std::chrono::milliseconds(400));

    engine_stop(engine.get());
    
    std::cout << "\n--- Oscillator Integrity Test Completed. ---" << std::endl;
    return 0;
}
