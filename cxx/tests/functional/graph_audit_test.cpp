#include "CInterface.h"
#include <iostream>
#include <vector>
#include <cmath>
#include <unistd.h>
#include <iomanip>

/**
 * @file graph_audit_test.cpp
 * @brief Performs a bottom-up audit of the audio path.
 */

float calculate_rms(const float* buffer, size_t frames) {
    float sum = 0.0f;
    for (size_t i = 0; i < frames; ++i) {
        sum += buffer[i] * buffer[i];
    }
    return std::sqrt(sum / frames);
}

int main() {
    std::cout << "--- Starting Bottom-Up Graph Audit ---" << std::endl;
    
    unsigned int sample_rate = 44100;
    EngineHandle engine = engine_create(sample_rate);
    
    if (!engine) {
        std::cerr << "Failed to create engine" << std::endl;
        return 1;
    }

    // Set high cutoff to ensure signal passes
    set_param(engine, "vcf_cutoff", 10000.0f);
    
    // Stage 1: Raw Oscillator Check
    std::cout << "\n>>> STAGE 1: Pulse Solo (Expect Audible C4) <<<" << std::endl;
    set_param(engine, "pulse_gain", 1.0f);
    set_param(engine, "saw_gain", 0.0f);
    set_param(engine, "sub_gain", 0.0f);
    
    engine_note_on(engine, 60, 1.0f); // Middle C
    
    const size_t frames = 512;
    std::vector<float> output(frames * 2); // Stereo
    
    for (int i = 0; i < 20; ++i) {
        engine_process(engine, output.data(), frames);
        float rms = calculate_rms(output.data(), frames);
        std::cout << "Block " << i << " RMS: " << std::fixed << std::setprecision(4) << rms << " ";
        if (rms > 0.0001f) std::cout << "[SIGNAL OK]";
        else std::cout << "[SILENT]";
        std::cout << std::endl;
        usleep(10000);
    }
    
    engine_note_off(engine, 60);
    std::cout << ">>> Stage 1 Complete <<<" << std::endl;

    // Stage 2: Oscillator + Envelope Check
    std::cout << "\n>>> STAGE 2: Pulse + Envelope (Gated C4) <<<" << std::endl;
    set_param(engine, "pulse_gain", 1.0f);
    
    engine_note_on(engine, 60, 1.0f);
    
    for (int i = 0; i < 20; ++i) {
        engine_process(engine, output.data(), frames);
        float rms = calculate_rms(output.data(), frames);
        std::cout << "Block " << i << " RMS: " << std::fixed << std::setprecision(4) << rms << std::endl;
        usleep(10000);
    }
    
    engine_note_off(engine, 60);
    // Wait for release
    for (int i = 0; i < 10; ++i) {
        engine_process(engine, output.data(), frames);
        float rms = calculate_rms(output.data(), frames);
        std::cout << "Release Block " << i << " RMS: " << std::fixed << std::setprecision(4) << rms << std::endl;
    }
    std::cout << ">>> Stage 2 Complete <<<" << std::endl;

    engine_destroy(engine);

    // Stage 3: Full Subtractive Path (Filter + Envelope)
    std::cout << "\n>>> STAGE 3: Full Subtractive Path (C4) <<<" << std::endl;
    engine = engine_create(sample_rate);
    set_param(engine, "vcf_cutoff", 500.0f); // Low cutoff to hear filter action
    set_param(engine, "pulse_gain", 1.0f);
    
    engine_note_on(engine, 60, 1.0f);
    
    for (int i = 0; i < 20; ++i) {
        engine_process(engine, output.data(), frames);
        float rms = calculate_rms(output.data(), frames);
        std::cout << "Block " << i << " RMS: " << std::fixed << std::setprecision(4) << rms << std::endl;
        usleep(10000);
    }
    
    engine_note_off(engine, 60);
    engine_destroy(engine);

    std::cout << "\n--- Audit Test Finished ---" << std::endl;
    return 0;
}
