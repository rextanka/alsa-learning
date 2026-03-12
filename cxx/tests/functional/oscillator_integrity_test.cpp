/**
 * @file oscillator_integrity_test.cpp
 * @brief Solo test for individual oscillators to verify they can make sound via Bridge API.
 */

#include "../TestHelper.hpp"
#include <iostream> 
#include <thread>
#include <chrono>

/**
 * @brief Refactored Stage Helper: Implements Sequential Voice Strategy.
 * Each stage is isolated with a total mixer reset and follows a musical ADSR lifecycle.
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
    std::this_thread::sleep_for(std::chrono::milliseconds(1800));
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

    // Wavetable testing: explicitly set to Saw table
    set_param(engine.get(), "wavetable_type", 1.0f); // 1 = Saw
    run_sequential_stage(engine.get(), "Wavetable (Interpolated Saw)", "wavetable_gain");

    // Sub-Oscillator: Phase Lock Logic (Requires Pulse parent)
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
    std::this_thread::sleep_for(std::chrono::milliseconds(2000));
    engine_note_off_name(engine.get(), "C4");
    std::this_thread::sleep_for(std::chrono::milliseconds(400));

    engine_stop(engine.get());
    
    std::cout << "\n--- Oscillator Integrity Test Completed. ---" << std::endl;
    return 0;
}
