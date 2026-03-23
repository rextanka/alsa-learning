/**
 * @file test_chime_patch.cpp
 * @brief Functional tests for chime.json — Roland Fig. 1-12.
 *
 * Patch topology (Roland System 100M Fig. 1-12):
 *   4× VCO (saw) at root/+12/+5/+8 semitones → AUDIO_MIXER → VCA ← ADSR (fast pluck)
 *   Pitch CV fanned via CV_SPLITTER to all 4 VCOs.
 *
 * Additive synthesis: four partials tuned to root, octave, perfect 4th, minor 6th
 * give the metallic bell-like chime character.
 *
 * Key assertions:
 *   1. Smoke    — note_on produces audio.
 *   2. Decay    — pluck envelope decays: block-0 RMS > block-16 RMS.
 *   3. Audible  — C major arpeggio.
 */

#include <gtest/gtest.h>
#include "../TestHelper.hpp"
#include "PatchAnalysis.hpp"
#include <vector>
#include <cmath>
#include <thread>
#include <chrono>

class ChimePatchTest : public ::testing::Test {
protected:
    void SetUp() override {
        test::init_test_environment();
        sample_rate = test::get_safe_sample_rate(0);
        engine_wrapper = std::make_unique<test::EngineWrapper>(sample_rate);
    }
    EngineHandle engine() { return engine_wrapper->get(); }
    int sample_rate;
    std::unique_ptr<test::EngineWrapper> engine_wrapper;
    static constexpr const char* kPatch = "patches/chime.json";
};

TEST_F(ChimePatchTest, NoteOnProducesAudio) {
    PRINT_TEST_HEADER(
        "Chime — Smoke",
        "4 VCOs additive: note_on produces audio.",
        "engine_load_patch → note_on(C4) → 5 blocks",
        "RMS > 0.001",
        sample_rate
    );

    ASSERT_EQ(engine_load_patch(engine(), kPatch), 0);
    engine_note_on(engine(), 60, 1.0f);  // C4

    const size_t FRAMES = 512;
    std::vector<float> buf(FRAMES * 2);
    double sum_sq = 0.0;
    for (int b = 0; b < 5; ++b) {
        engine_process(engine(), buf.data(), FRAMES);
        for (size_t i = 0; i < FRAMES; ++i) sum_sq += double(buf[i*2]) * double(buf[i*2]);
    }
    engine_note_off(engine(), 60);

    float rms = float(std::sqrt(sum_sq / double(FRAMES * 5)));
    std::cout << "[Chime] Onset RMS: " << rms << "\n";
    EXPECT_GT(rms, 0.001f);
}

TEST_F(ChimePatchTest, PluckDecays) {
    PRINT_TEST_HEADER(
        "Chime — Pluck Decay",
        "S=0 envelope: signal decays after initial transient.",
        "note_on(C4) → compare block 0 vs block 16",
        "rms_block0 > rms_block16",
        sample_rate
    );

    ASSERT_EQ(engine_load_patch(engine(), kPatch), 0);
    engine_note_on(engine(), 60, 1.0f);

    const size_t FRAMES = 512;
    std::vector<float> buf(FRAMES * 2);
    float rms_early = 0.0f, rms_late = 0.0f;

    for (int b = 0; b < 20; ++b) {
        engine_process(engine(), buf.data(), FRAMES);
        double sq = 0.0;
        for (size_t i = 0; i < FRAMES; ++i) sq += double(buf[i*2]) * double(buf[i*2]);
        float rms = float(std::sqrt(sq / FRAMES));
        if (b == 2)  rms_early = rms;
        if (b == 16) rms_late  = rms;
    }
    engine_note_off(engine(), 60);

    std::cout << "[Chime] Block-2 RMS:  " << rms_early << "\n";
    std::cout << "[Chime] Block-16 RMS: " << rms_late  << "\n";
    EXPECT_GT(rms_early, rms_late) << "Pluck envelope should decay to near silence";
}

TEST_F(ChimePatchTest, ArpeggioAudible) {
    PRINT_TEST_HEADER(
        "Chime — C Major Arpeggio (audible)",
        "Bright metallic chime tone with reverb tail.",
        "engine_load_patch → engine_start → C4 E4 G4 C5",
        "Audible bell-like chime arpeggio.",
        sample_rate
    );

    ASSERT_EQ(engine_load_patch(engine(), kPatch), 0);
    ASSERT_ENGINE_START(engine());

    const int notes[] = {60, 64, 67, 72, 67, 64, 60};
    std::cout << "[Chime] Playing C major arpeggio…\n";
    for (int midi : notes) {
        engine_note_on(engine(), midi, 0.9f);
        std::this_thread::sleep_for(std::chrono::milliseconds(400));
        engine_note_off(engine(), midi);
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    // Let the last chime ring out through its 10s decay
    std::cout << "[Chime] Ringing out…\n";
    std::this_thread::sleep_for(std::chrono::milliseconds(6000));

    engine_stop(engine());
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
