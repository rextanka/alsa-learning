/**
 * @file oscillator_integrity_test.cpp
 * @brief Solo test for individual oscillators to verify they can make sound via Bridge API.
 *
 * Chain: COMPOSITE_GENERATOR -> ADSR_ENVELOPE -> VCA (Phase 15)
 * Each waveform is isolated by setting all gains to 0 then enabling one at a time.
 */

#include "../TestHelper.hpp"
#include <iostream>
#include <thread>
#include <chrono>
#include <vector>
#include <cmath>
#include <cassert>

static float calculate_rms(const float* buffer, size_t frames) {
    if (frames == 0) return 0.0f;
    float sum = 0.0f;
    for (size_t i = 0; i < frames; ++i)
        sum += buffer[i] * buffer[i];
    return std::sqrt(sum / static_cast<float>(frames));
}

/**
 * @brief Isolate one waveform, trigger C4, capture RMS, assert non-silent.
 */
static void run_sequential_stage(EngineHandle engine, const char* name,
                                  const char* gain_param, float level = 1.0f) {
    std::cout << "\n>>> STAGE: " << name << " <<<" << std::endl;

    // Total mixer reset to prevent state leakage
    const char* all_gains[] = {
        "pulse_gain", "saw_gain", "sub_gain",
        "sine_gain", "triangle_gain", "wavetable_gain"
    };
    for (auto g : all_gains) set_param(engine, g, 0.0f);

    engine_audiotap_reset(engine);

    set_param(engine, "amp_attack",  0.05f);
    set_param(engine, "amp_decay",   1.5f);
    set_param(engine, "amp_sustain", 0.0f);
    set_param(engine, "vcf_cutoff",  2000.0f);
    set_param(engine, "vcf_res",     0.2f);
    set_param(engine, gain_param, level);

    std::cout << "  Triggering C4..." << std::endl;
    engine_note_on_name(engine, "C4", 0.8f);

    std::this_thread::sleep_for(std::chrono::milliseconds(500));

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
        "COMPOSITE_GENERATOR -> ADSR_ENVELOPE -> VCA -> Output",
        "Sequence of isolated plucks for Sine, Triangle, Pulse, Saw, Wavetable, and Sub.",
        sample_rate
    );

    test::EngineWrapper engine(sample_rate);

    // Phase 15 chain
    engine_add_module(engine.get(), "COMPOSITE_GENERATOR", "VCO");
    engine_add_module(engine.get(), "ADSR_ENVELOPE",       "ENV");
    engine_add_module(engine.get(), "VCA",                 "VCA");
    engine_connect_ports(engine.get(), "ENV", "envelope_out", "VCA", "gain_cv");
    engine_bake(engine.get());

    if (engine_start(engine.get()) != 0) {
        std::cerr << "Failed to start audio engine" << std::endl;
        return 1;
    }

    run_sequential_stage(engine.get(), "Pure Sine (Rotor)",       "sine_gain");
    run_sequential_stage(engine.get(), "Triangle (Naive)",        "triangle_gain");
    run_sequential_stage(engine.get(), "Pulse (PolyBLEP)",        "pulse_gain");
    run_sequential_stage(engine.get(), "Sawtooth (PolyBLEP)",     "saw_gain");
    run_sequential_stage(engine.get(), "Wavetable",               "wavetable_gain");

    // Sub-oscillator stage: requires Pulse parent to be active
    std::cout << "\n>>> STAGE: Sub + Pulse (Phase Lock) <<<" << std::endl;
    engine_audiotap_reset(engine.get());
    const char* all_gains[] = {
        "pulse_gain", "saw_gain", "sub_gain",
        "sine_gain", "triangle_gain", "wavetable_gain"
    };
    for (auto g : all_gains) set_param(engine.get(), g, 0.0f);

    set_param(engine.get(), "pulse_gain",    0.5f);
    set_param(engine.get(), "sub_gain",      0.5f);
    set_param(engine.get(), "amp_attack",  0.01f);
    set_param(engine.get(), "amp_decay",   1.5f);
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
