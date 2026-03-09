#include "../TestHelper.hpp"
#include <iostream>
#include <vector>
#include <cmath>
#include <iomanip>

/**
 * @file graph_audit_test.cpp
 * @brief Performs a bottom-up audit of the audio path using Bridge API.
 */

float calculate_rms(const float* buffer, size_t frames) {
    float sum = 0.0f;
    for (size_t i = 0; i < frames; ++i) {
        sum += buffer[i] * buffer[i];
    }
    return std::sqrt(sum / frames);
}

int main() {
    test::init_test_environment();
    int sample_rate = test::get_safe_sample_rate(0);

    PRINT_TEST_HEADER(
        "Bottom-Up Graph Audit",
        "Step-by-step verification of signal flow from Oscillator to Output.",
        "VCO -> VCF -> VCA -> Bridge API -> Output",
        "RMS values > 0 at each stage confirming signal integrity.",
        sample_rate
    );
    
    test::EngineWrapper engine(sample_rate);

    // Protocol Step 3 & 4: Modular Patching & ADSR Arming
    engine_connect_mod(engine.get(), MOD_SRC_ENVELOPE, ALL_VOICES, MOD_TGT_AMPLITUDE, 1.0f);
    set_param(engine.get(), "amp_sustain", 1.0f);

    // Protocol Step 5: Lifecycle Start
    if (engine_start(engine.get()) != 0) {
        std::cerr << "Failed to start engine" << std::endl;
        return 1;
    }

    // Set high cutoff to ensure signal passes
    set_param(engine.get(), "vcf_cutoff", 10000.0f);
    
    // Stage 1: Raw Oscillator Check
    std::cout << "\n>>> STAGE 1: Pulse Solo (Expect Audible C4) <<<" << std::endl;
    set_param(engine.get(), "pulse_gain", 1.0f);
    set_param(engine.get(), "saw_gain", 0.0f);
    set_param(engine.get(), "sub_gain", 0.0f);
    
    engine_note_on(engine.get(), 60, 1.0f); // Middle C
    
    const size_t frames = 512;
    std::vector<float> output(frames * 2); // Stereo
    
    for (int i = 0; i < 10; ++i) {
        engine_process(engine.get(), output.data(), frames);
        float rms = calculate_rms(output.data(), frames);
        std::cout << "Block " << i << " RMS: " << std::fixed << std::setprecision(4) << rms << " ";
        if (rms > 0.0001f) std::cout << "[SIGNAL OK]";
        else std::cout << "[SILENT]";
        std::cout << std::endl;
    }
    
    engine_note_off(engine.get(), 60);
    std::cout << ">>> Stage 1 Complete <<<" << std::endl;

    // Stage 2: Full Subtractive Path (Filter + Envelope)
    std::cout << "\n>>> STAGE 2: Full Subtractive Path (C4) <<<" << std::endl;
    set_param(engine.get(), "vcf_cutoff", 500.0f); // Low cutoff to hear filter action
    set_param(engine.get(), "pulse_gain", 1.0f);
    
    engine_note_on(engine.get(), 60, 1.0f);
    
    for (int i = 0; i < 10; ++i) {
        engine_process(engine.get(), output.data(), frames);
        float rms = calculate_rms(output.data(), frames);
        std::cout << "Block " << i << " RMS: " << std::fixed << std::setprecision(4) << rms << std::endl;
    }
    
    engine_note_off(engine.get(), 60);
    std::cout << ">>> Stage 2 Complete <<<" << std::endl;

    std::cout << "\n--- Audit Test Finished. Engine destroyed via RAII. ---" << std::endl;
    return 0;
}
