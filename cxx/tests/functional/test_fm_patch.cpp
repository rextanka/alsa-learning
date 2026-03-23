/**
 * @file test_fm_patch.cpp
 * @brief Functional tests for fm.json — Roland Fig. 2-5.
 *
 * Patch topology (Roland System 100M Fig. 2-5):
 *   VCO-2 (modulator, sine) → VCO-1 fm_in (carrier, sine) → VCA ← ADSR
 *   Both VCOs track pitch via CV_SPLITTER.
 *
 * FM produces inharmonic overtones useful for metallic/bell sounds.
 * Higher fm_depth = more complex/detuned spectrum.
 *
 * Key assertions:
 *   1. Smoke   — note_on produces audio.
 *   2. Audible — C major arpeggio, metallic FM character.
 */

#include <gtest/gtest.h>
#include "../TestHelper.hpp"
#include "PatchAnalysis.hpp"
#include <vector>
#include <cmath>
#include <thread>
#include <chrono>

class FmPatchTest : public ::testing::Test {
protected:
    void SetUp() override {
        test::init_test_environment();
        sample_rate = test::get_safe_sample_rate(0);
        engine_wrapper = std::make_unique<test::EngineWrapper>(sample_rate);
    }
    EngineHandle engine() { return engine_wrapper->get(); }
    int sample_rate;
    std::unique_ptr<test::EngineWrapper> engine_wrapper;
    static constexpr const char* kPatch = "patches/fm.json";
};

TEST_F(FmPatchTest, NoteOnProducesAudio) {
    PRINT_TEST_HEADER(
        "FM — Smoke",
        "VCO-2 modulates VCO-1: note_on produces audio.",
        "engine_load_patch → note_on(C4) → 5 blocks",
        "RMS > 0.001",
        sample_rate
    );
    ASSERT_EQ(engine_load_patch(engine(), kPatch), 0);
    engine_note_on(engine(), 60, 1.0f);

    const size_t FRAMES = 512;
    std::vector<float> buf(FRAMES * 2);
    double sum_sq = 0.0;
    for (int b = 0; b < 5; ++b) {
        engine_process(engine(), buf.data(), FRAMES);
        for (size_t i = 0; i < FRAMES; ++i) sum_sq += double(buf[i*2]) * double(buf[i*2]);
    }
    engine_note_off(engine(), 60);
    float rms = float(std::sqrt(sum_sq / double(FRAMES * 5)));
    std::cout << "[FM] Onset RMS: " << rms << "\n";
    EXPECT_GT(rms, 0.001f);
}

TEST_F(FmPatchTest, ArpeggioAudible) {
    PRINT_TEST_HEADER(
        "FM — C Major Arpeggio (audible)",
        "Metallic FM timbre with inharmonic overtones.",
        "engine_load_patch → engine_start → C4 E4 G4 C5",
        "Audible metallic FM tone.",
        sample_rate
    );
    ASSERT_EQ(engine_load_patch(engine(), kPatch), 0);
    ASSERT_ENGINE_START(engine());

    const int notes[] = {60, 64, 67, 72, 67, 64, 60};
    std::cout << "[FM] Playing C major arpeggio…\n";
    for (int midi : notes) {
        engine_note_on(engine(), midi, 0.85f);
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        engine_note_off(engine(), midi);
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    engine_stop(engine());
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
