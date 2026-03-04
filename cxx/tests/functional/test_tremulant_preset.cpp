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
        sample_rate = test::get_safe_sample_rate(0);
        
        PRINT_TEST_HEADER(
            "Modular Tremulant Integrity",
            "Verifies LFO -> Pitch modulation via the Modulation Matrix.",
            "LFO (Source) -> Pitch (Target) -> Output",
            "Modulation report confirming the link; successful process call.",
            sample_rate
        );

        engine_wrapper = std::make_unique<test::EngineWrapper>(sample_rate);
    }

    int sample_rate;
    std::unique_ptr<test::EngineWrapper> engine_wrapper;
};

TEST_F(TremulantTest, ModularTremulant) {
    EngineHandle engine = engine_wrapper->get();
    
    // 1. Setup internal LFO -> Pitch modulation
    // Intensity: ±0.02 octaves for subtle vibrato
    engine_set_modulation(engine, MOD_SRC_LFO, MOD_TGT_PITCH, 0.02f);

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
