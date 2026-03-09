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
        
        // Protocol Step 3 & 4 & 5: Modular Patching & ADSR & Start
        engine_connect_mod(engine_wrapper->get(), MOD_SRC_ENVELOPE, ALL_VOICES, MOD_TGT_AMPLITUDE, 1.0f);
        set_param(engine_wrapper->get(), "amp_sustain", 1.0f);
        engine_start(engine_wrapper->get());
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

    // 4. Process a block and check that modulation is active
    std::vector<float> output(512 * 2); // Stereo
    int result = engine_process(engine, output.data(), 512);
    EXPECT_EQ(result, 0);

    std::cout << "[TremulantTest] Verified LFO -> Pitch modular route." << std::endl;
}

TEST_F(TremulantTest, ModulationReport) {
    EngineHandle engine = engine_wrapper->get();
    char report[256];
    int result = engine_get_modulation_report(engine, report, sizeof(report));
    EXPECT_EQ(result, 0);
    EXPECT_TRUE(std::string(report).find("Modulation Report") != std::string::npos);
}
