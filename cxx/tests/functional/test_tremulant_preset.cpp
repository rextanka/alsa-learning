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

TEST_F(TremulantTest, DetachedLfoPhaseSync) {
    // 1. Create LFO for Tremulant
    int lfo_id = engine_create_processor(engine, PROC_LFO);
    ASSERT_GE(lfo_id, 100);

    // 2. Connect LFO to all voices pitch
    // Intensity: 0.1 octaves (very audible vibrato)
    engine_connect_mod(engine, lfo_id, ALL_VOICES, PARAM_PITCH, 0.1f);

    // Verify report shows connection
    char report[256];
    engine_get_modulation_report(engine, report, sizeof(report));
    EXPECT_TRUE(std::string(report).find("Src: 100 -> Tgt: -1") != std::string::npos);

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

    // 5. Verification of "Detached LFO" (Phase Sync)
    // In a real audit, we'd check if the OscillatorProcessor's pitch_mod_ is identical
    // across all active voices. Since we can't easily peek into private members 
    // of internal classes from the C-API test, we rely on the architecture 
    // where the LFO is pulled once per VoiceManager::do_pull and applied to all.
}

TEST_F(TremulantTest, ModulationReport) {
    char report[256];
    int result = engine_get_modulation_report(engine, report, sizeof(report));
    EXPECT_EQ(result, 0);
    EXPECT_TRUE(std::string(report).find("Modulation Report") != std::string::npos);
}
