/**
 * @file test_bassoon_patch.cpp
 * @brief Functional tests for bassoon.json — Roland Fig. 1-10.
 *
 * Patch topology (Roland System 100M Fig. 1-10):
 *   VCO (saw, 32') → VCF (fixed ~900Hz) → VCA ← ADSR (A=1s, D=100ms, S=1.0, R=1s)
 *
 * No filter envelope — cutoff is static, giving the warm woody bassoon timbre.
 * 32' footage drops the VCO two octaves below concert pitch into the deep bassoon register.
 *
 * Key assertions:
 *   1. Smoke      — note_on produces audio.
 *   2. FullSustain — A=1ms, S=1.0: block-8 RMS > 80% of block-4 RMS (held full sustain).
 *   3. Audible     — C2 ascending scale.
 */

#include <gtest/gtest.h>
#include "../TestHelper.hpp"
#include "PatchAnalysis.hpp"
#include <vector>
#include <cmath>
#include <thread>
#include <chrono>

class BassoonPatchTest : public ::testing::Test {
protected:
    void SetUp() override {
        test::init_test_environment();
        sample_rate = test::get_safe_sample_rate(0);
        engine_wrapper = std::make_unique<test::EngineWrapper>(sample_rate);
    }
    EngineHandle engine() { return engine_wrapper->get(); }
    int sample_rate;
    std::unique_ptr<test::EngineWrapper> engine_wrapper;
    static constexpr const char* kPatch = "patches/bassoon.json";
};

TEST_F(BassoonPatchTest, NoteOnProducesAudio) {
    PRINT_TEST_HEADER(
        "Bassoon — Smoke",
        "VCO saw 16' → VCF (fixed) → VCA ← ADSR: note_on produces audio.",
        "engine_load_patch → note_on(C3) → 5 blocks",
        "RMS > 0.001",
        sample_rate
    );

    ASSERT_EQ(engine_load_patch(engine(), kPatch), 0);
    engine_note_on(engine(), 48, 1.0f);  // C3

    const size_t FRAMES = 512;
    std::vector<float> buf(FRAMES * 2);
    double sum_sq = 0.0;
    for (int b = 0; b < 5; ++b) {
        engine_process(engine(), buf.data(), FRAMES);
        for (size_t i = 0; i < FRAMES; ++i) sum_sq += double(buf[i*2]) * double(buf[i*2]);
    }
    engine_note_off(engine(), 48);

    float rms = float(std::sqrt(sum_sq / double(FRAMES * 5)));
    std::cout << "[Bassoon] Onset RMS: " << rms << "\n";
    EXPECT_GT(rms, 0.001f);
}

TEST_F(BassoonPatchTest, FullSustain) {
    PRINT_TEST_HEADER(
        "Bassoon — Full Sustain (S=1.0)",
        "A=1ms, S=1.0: signal holds at full level after attack.",
        "note_on(C3) → compare block 4 vs block 8",
        "rms_block8 > 0.8 × rms_block4",
        sample_rate
    );

    ASSERT_EQ(engine_load_patch(engine(), kPatch), 0);
    engine_note_on(engine(), 48, 1.0f);

    const size_t FRAMES = 512;
    std::vector<float> buf(FRAMES * 2);
    float rms_mid = 0.0f, rms_late = 0.0f;

    for (int b = 0; b < 12; ++b) {
        engine_process(engine(), buf.data(), FRAMES);
        double sq = 0.0;
        for (size_t i = 0; i < FRAMES; ++i) sq += double(buf[i*2]) * double(buf[i*2]);
        float rms = float(std::sqrt(sq / FRAMES));
        if (b == 4) rms_mid  = rms;
        if (b == 8) rms_late = rms;
    }
    engine_note_off(engine(), 48);

    std::cout << "[Bassoon] Block-4 RMS: " << rms_mid  << "\n";
    std::cout << "[Bassoon] Block-8 RMS: " << rms_late << "\n";
    EXPECT_GT(rms_late, 0.8f * rms_mid) << "S=1.0: sustain should hold at full level";
}

TEST_F(BassoonPatchTest, ScaleAudible) {
    PRINT_TEST_HEADER(
        "Bassoon — C2 Scale (audible)",
        "Warm dark woody bassoon tone in low register, slow breath attack.",
        "engine_load_patch → engine_start → C2..C3 ascending",
        "Audible deep warm bassoon tone.",
        sample_rate
    );

    ASSERT_EQ(engine_load_patch(engine(), kPatch), 0);
    ASSERT_ENGINE_START(engine());

    // C3 ascending — one octave up from 32' sub-bass
    const int notes[] = {48, 50, 52, 55, 57, 60};
    std::cout << "[Bassoon] Playing C3 ascending scale…\n";
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
