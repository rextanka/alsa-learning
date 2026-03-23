/**
 * @file test_violin_patch.cpp
 * @brief Functional tests for violin.json — Roland Fig. 2-2.
 *
 * Patch topology (Roland System 100M Fig. 2-2):
 *   VCO (saw, 8') → VCF (LP) → VCA ← ADSR
 *   LFO (delayed vibrato, delay=0.5s) → VCO pitch_cv
 *   Gate → LFO reset (restarts delay on each note)
 *
 * Key assertions:
 *   1. Smoke    — note_on produces audio.
 *   2. Audible  — sustained phrase, vibrato fades in after ~0.5s.
 */

#include <gtest/gtest.h>
#include "../TestHelper.hpp"
#include "PatchAnalysis.hpp"
#include <vector>
#include <cmath>
#include <thread>
#include <chrono>

class ViolinPatchTest : public ::testing::Test {
protected:
    void SetUp() override {
        test::init_test_environment();
        sample_rate = test::get_safe_sample_rate(0);
        engine_wrapper = std::make_unique<test::EngineWrapper>(sample_rate);
    }
    EngineHandle engine() { return engine_wrapper->get(); }
    int sample_rate;
    std::unique_ptr<test::EngineWrapper> engine_wrapper;
    static constexpr const char* kPatch = "patches/violin.json";
};

TEST_F(ViolinPatchTest, NoteOnProducesAudio) {
    PRINT_TEST_HEADER(
        "Violin — Smoke",
        "VCO saw → VCF → VCA ← ADSR: note_on produces audio.",
        "engine_load_patch → note_on(E4) → 5 blocks",
        "RMS > 0.001",
        sample_rate
    );
    ASSERT_EQ(engine_load_patch(engine(), kPatch), 0);
    engine_note_on(engine(), 64, 1.0f);

    const size_t FRAMES = 512;
    std::vector<float> buf(FRAMES * 2);
    double sum_sq = 0.0;
    for (int b = 0; b < 5; ++b) {
        engine_process(engine(), buf.data(), FRAMES);
        for (size_t i = 0; i < FRAMES; ++i) sum_sq += double(buf[i*2]) * double(buf[i*2]);
    }
    engine_note_off(engine(), 64);
    float rms = float(std::sqrt(sum_sq / double(FRAMES * 5)));
    std::cout << "[Violin] Onset RMS: " << rms << "\n";
    EXPECT_GT(rms, 0.001f);
}

TEST_F(ViolinPatchTest, PhraseAudible) {
    PRINT_TEST_HEADER(
        "Violin — Sustained Phrase (audible)",
        "Warm violin tone with delayed vibrato fading in after ~0.5s.",
        "engine_load_patch → engine_start → E4 G4 A4 B4 C5",
        "Audible violin with vibrato onset.",
        sample_rate
    );
    ASSERT_EQ(engine_load_patch(engine(), kPatch), 0);
    ASSERT_ENGINE_START(engine());

    const int notes[] = {64, 67, 69, 71, 72};
    std::cout << "[Violin] Playing sustained phrase (listen for vibrato fade-in)…\n";
    for (int midi : notes) {
        engine_note_on(engine(), midi, 0.8f);
        std::this_thread::sleep_for(std::chrono::milliseconds(1200));
        engine_note_off(engine(), midi);
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
    }

    engine_stop(engine());
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
