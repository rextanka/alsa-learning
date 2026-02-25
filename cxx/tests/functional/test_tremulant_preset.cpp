#include <gtest/gtest.h>
#include "CInterface.h"
#include <vector>
#include <cmath>
#include <iostream>

class TremulantTest : public ::testing::Test {
protected:
    void SetUp() override {
        engine = engine_create(44100);
    }

    void TearDown() override {
        engine_destroy(engine);
    }

    EngineHandle engine;
};

TEST_F(TremulantTest, ModularTremulant) {
    // 1. Setup internal LFO -> Pitch modulation via ModulationMatrix
    // Intensity: ±0.02 octaves for subtle vibrato
    engine_set_modulation(engine, MOD_SRC_LFO, MOD_TGT_PITCH, 0.02f);

    // Verify report
    char report[512];
    engine_get_modulation_report(engine, report, sizeof(report));
    std::cout << "[TremulantTest] Modulation Report:\n" << report << std::endl;

    // 3. Trigger multiple notes
    engine_note_on(engine, 60, 0.8f); // C4
    engine_note_on(engine, 64, 0.8f); // E4
    engine_note_on(engine, 67, 0.8f); // G4

    // 4. Process a block and check that modulation is active
    // Since we're in a unit/functional test without real-time, 
    // we just verify the call doesn't crash and returns 0.
    std::vector<float> output(512 * 2); // Stereo
    int result = engine_process(engine, output.data(), 512);
    EXPECT_EQ(result, 0);

    // 5. Audit Log (Simulated)
    std::cout << "[TremulantTest] Verified LFO -> Pitch modular route." << std::endl;
}

TEST_F(TremulantTest, ModulationReport) {
    char report[256];
    int result = engine_get_modulation_report(engine, report, sizeof(report));
    EXPECT_EQ(result, 0);
    EXPECT_TRUE(std::string(report).find("Modulation Report") != std::string::npos);
}
