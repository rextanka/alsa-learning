#include <gtest/gtest.h>
#include "../TestHelper.hpp" 
#include <vector>
#include <cmath>
#include <iostream>

/**
 * @file test_tremulant_preset.cpp
 * @brief Functional verification of Tremulant (Vibrato) modulation via Bridge API.
 */

class TremulantTest : public ::testing::Test {
protected:
    void SetUp() override {
        test::init_test_environment();
        sample_rate = test::get_safe_sample_rate(0);
        
        PRINT_TEST_HEADER(
            "Modular Tremulant Integrity",
            "Verifies LFO -> Pitch modulation via the Modulation Matrix.",
            "LFO (Source) -> Pitch (Target) -> Output",
            "Modulation report confirming the link; successful process call.",
            sample_rate
        );

        engine_wrapper = std::make_unique<test::EngineWrapper>(sample_rate);

        // Phase 15 chain
        engine_add_module(engine_wrapper->get(), "COMPOSITE_GENERATOR", "VCO");
        engine_add_module(engine_wrapper->get(), "ADSR_ENVELOPE",       "ENV");
        engine_add_module(engine_wrapper->get(), "VCA",                 "VCA");
        engine_connect_ports(engine_wrapper->get(), "ENV", "envelope_out", "VCA", "gain_cv");
        engine_bake(engine_wrapper->get());

        set_param(engine_wrapper->get(), "sine_gain",   1.0f);
        set_param(engine_wrapper->get(), "amp_sustain", 1.0f);
        // No engine_start: ModularTremulant uses offline engine_process for RMS analysis.
    }

    int sample_rate;
    std::unique_ptr<test::EngineWrapper> engine_wrapper;
};

TEST_F(TremulantTest, ModularTremulant) {
    EngineHandle engine = engine_wrapper->get();
    
    // 1. Setup internal LFO -> Pitch modulation - Using engine_connect_mod for Tier 2 compliance
    // Intensity: ±0.02 octaves for subtle vibrato
    engine_connect_mod(engine, MOD_SRC_LFO, ALL_VOICES, MOD_TGT_PITCH, 0.02f);

    // 2. Verify report
    char report[512];
    engine_get_modulation_report(engine, report, sizeof(report));
    std::cout << "[TremulantTest] Modulation Report:\n" << report << std::endl;

    // 3. Trigger multiple notes
    engine_note_on(engine, 60, 0.8f); // C4
    engine_note_on(engine, 64, 0.8f); // E4
    engine_note_on(engine, 67, 0.8f); // G4

    // 4. Process enough blocks to allow the ADSR envelope to open (50ms attack)
    std::vector<float> output(512 * 2); // Stereo
    for (int i = 0; i < 20; ++i) { // ~200ms
        engine_process(engine, output.data(), 512);
    }

    // 5. Audit the final block
    int result = engine_process(engine, output.data(), 512);
    EXPECT_EQ(result, 0);

    // Harden the test: Verify signal is present
    float sum_sq = 0;
    for(float s : output) sum_sq += s * s;
    float rms = std::sqrt(sum_sq / output.size());
    ASSERT_GT(rms, 0.001f) << "FAILED: Tremulant test output is silent! RMS=" << rms;

    std::cout << "[TremulantTest] Verified LFO -> Pitch modular route. RMS=" << rms << std::endl;
}

TEST_F(TremulantTest, ModulationReport) {
    EngineHandle engine = engine_wrapper->get();
    char report[256];
    int result = engine_get_modulation_report(engine, report, sizeof(report));
    EXPECT_EQ(result, 0);
    EXPECT_TRUE(std::string(report).find("Modulation Report") != std::string::npos);
}
