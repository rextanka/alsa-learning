/**
 * @file test_recorder_patch.cpp
 * @brief Functional tests for recorder.json — Roland Fig. 1-4.
 *
 * Patch topology (Roland System 100M Fig. 1-4):
 *   VCO (square, PW=50%) → VCA ← ADSR (A=30ms, D=0, S=1.0, R=100ms)
 *
 * No VCF — the hollow recorder timbre comes purely from the square wave.
 * Linear VCA mode gives a natural breath-like amplitude curve.
 *
 * Key assertions:
 *   1. Smoke       — note_on produces audio.
 *   2. NoFilter    — spectral content is broadband (square wave harmonics present);
 *                    energy above 2× fundamental confirms no low-pass cutoff.
 *   3. SoftAttack  — RMS at block 0 < 50% of RMS at block 5 (30ms breath onset).
 *   4. Audible     — G major pentatonic melody.
 */

#include <gtest/gtest.h>
#include "../TestHelper.hpp"
#include "PatchAnalysis.hpp"
#include <vector>
#include <cmath>
#include <thread>
#include <chrono>

class RecorderPatchTest : public ::testing::Test {
protected:
    void SetUp() override {
        test::init_test_environment();
        sample_rate = test::get_safe_sample_rate(0);
        engine_wrapper = std::make_unique<test::EngineWrapper>(sample_rate);
    }
    EngineHandle engine() { return engine_wrapper->get(); }
    int sample_rate;
    std::unique_ptr<test::EngineWrapper> engine_wrapper;
    static constexpr const char* kPatch = "patches/recorder.json";
};

// ---------------------------------------------------------------------------
// Test 1: Smoke
// ---------------------------------------------------------------------------

TEST_F(RecorderPatchTest, NoteOnProducesAudio) {
    PRINT_TEST_HEADER(
        "Recorder — Smoke",
        "VCO square → VCA ← ADSR: note_on produces audio.",
        "engine_load_patch → note_on(G4) → 5 blocks",
        "RMS > 0.001",
        sample_rate
    );

    ASSERT_EQ(engine_load_patch(engine(), kPatch), 0);
    engine_note_on(engine(), 67, 1.0f);  // G4

    const size_t FRAMES = 512;
    std::vector<float> buf(FRAMES * 2);
    double sum_sq = 0.0;
    for (int b = 0; b < 5; ++b) {
        engine_process(engine(), buf.data(), FRAMES);
        for (size_t i = 0; i < FRAMES; ++i) sum_sq += double(buf[i*2]) * double(buf[i*2]);
    }
    engine_note_off(engine(), 67);

    float rms = float(std::sqrt(sum_sq / double(FRAMES * 5)));
    std::cout << "[Recorder] Onset RMS: " << rms << "\n";
    EXPECT_GT(rms, 0.001f);
}

// ---------------------------------------------------------------------------
// Test 2: NoFilter — square wave harmonics present above 2× fundamental
// ---------------------------------------------------------------------------

TEST_F(RecorderPatchTest, NoFilterHarmonicsPresent) {
    PRINT_TEST_HEADER(
        "Recorder — No Filter / Square Wave Harmonics",
        "No VCF: square wave energy present well above fundamental.",
        "note_on(G4=392Hz) → capture 2048-sample window → centroid > 2× fund",
        "centroid > 392 × 2 = 784 Hz",
        sample_rate
    );

    ASSERT_EQ(engine_load_patch(engine(), kPatch), 0);
    engine_note_on(engine(), 67, 1.0f);  // G4 = 392 Hz

    const size_t BLOCK  = 512;
    const size_t WINDOW = 2048;
    std::vector<float> buf(BLOCK * 2);
    std::vector<float> win;
    win.reserve(WINDOW);

    for (int b = 0; b < 10; ++b) {
        engine_process(engine(), buf.data(), BLOCK);
        // Capture after attack settles (block 4+)
        if (b >= 4 && win.size() < WINDOW)
            for (size_t i = 0; i < BLOCK && win.size() < WINDOW; ++i)
                win.push_back(buf[i*2]);
    }
    engine_note_off(engine(), 67);

    ASSERT_EQ(win.size(), WINDOW);
    float centroid = spectral_centroid(win, sample_rate);
    constexpr float kG4Hz = 392.0f;

    std::cout << "[Recorder] G4 fundamental: " << kG4Hz   << " Hz\n";
    std::cout << "[Recorder] Centroid:        " << centroid << " Hz\n";
    std::cout << "[Recorder] Centroid/fund:   " << centroid / kG4Hz << "\n";

    EXPECT_GT(centroid, kG4Hz * 2.0f)
        << "Square wave should have energy well above fundamental (no filter cutoff)";
}

// ---------------------------------------------------------------------------
// Test 3: SoftAttack — 30ms breath onset
// ---------------------------------------------------------------------------

TEST_F(RecorderPatchTest, SoftAttack) {
    PRINT_TEST_HEADER(
        "Recorder — Soft Attack (30ms)",
        "ADSR attack=30ms: block 0 RMS < 50% of block 5 RMS.",
        "note_on → compare block 0 vs block 5",
        "rms_block0 < 0.5 × rms_block5",
        sample_rate
    );

    ASSERT_EQ(engine_load_patch(engine(), kPatch), 0);
    engine_note_on(engine(), 67, 1.0f);

    const size_t FRAMES = 512;
    std::vector<float> buf(FRAMES * 2);
    float rms_early = 0.0f, rms_late = 0.0f;

    for (int b = 0; b < 8; ++b) {
        engine_process(engine(), buf.data(), FRAMES);
        double sq = 0.0;
        for (size_t i = 0; i < FRAMES; ++i) sq += double(buf[i*2]) * double(buf[i*2]);
        float rms = float(std::sqrt(sq / FRAMES));
        if (b == 0) rms_early = rms;
        if (b == 5) rms_late  = rms;
    }
    engine_note_off(engine(), 67);

    std::cout << "[Recorder] Block-0 RMS (onset): " << rms_early << "\n";
    std::cout << "[Recorder] Block-5 RMS (sustain): " << rms_late << "\n";

    EXPECT_LT(rms_early, 0.5f * rms_late) << "30ms attack: block 0 should be quieter than block 5";
}

// ---------------------------------------------------------------------------
// Test 4: Audible — G major pentatonic
// ---------------------------------------------------------------------------

TEST_F(RecorderPatchTest, PentatonicMelodyAudible) {
    PRINT_TEST_HEADER(
        "Recorder — G Major Pentatonic (audible)",
        "Simple recorder: hollow square-wave tone, soft breath attack, no filter.",
        "engine_load_patch → engine_start → G4/A4/B4/D5/E5 half notes",
        "Audible clear hollow recorder tone.",
        sample_rate
    );

    ASSERT_EQ(engine_load_patch(engine(), kPatch), 0);
    ASSERT_ENGINE_START(engine());

    const int notes[] = {67, 69, 71, 74, 76, 74, 71, 69, 67};
    std::cout << "[Recorder] Playing G pentatonic melody…\n";
    for (int midi : notes) {
        engine_note_on(engine(), midi, 0.75f);
        std::this_thread::sleep_for(std::chrono::milliseconds(400));
        engine_note_off(engine(), midi);
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
    }

    engine_stop(engine());
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
