/**
 * @file test_oboe_patch.cpp
 * @brief Functional tests for oboe.json — nasal double-reed timbre with HPF.
 *
 * Patch topology (Roland System 100M Fig 1-9, Oboe variant):
 *   COMPOSITE_GENERATOR (pulse 35%) → MOOG_FILTER (cutoff=1200Hz, res=0.92)
 *       → HIGH_PASS_FILTER (cutoff=250Hz)   ← VCF built-in HPF switch at "1"
 *       → VCA ← ADSR (A=30ms, D=100ms, S=0.85, R=150ms)
 *
 * Oboe differs from English Horn by:
 *   - Pulse waveform (35%) instead of sawtooth — double-reed harmonic character
 *   - MOOG_FILTER instead of SH_FILTER — tanh saturation gives buzzy reedy edge
 *   - Higher resonance (0.92 vs 0.5) — pronounced resonant peak at 1200Hz
 *   - HPF (250Hz) slightly suppresses fundamental per book description
 *
 * Tests:
 *   1. Smoke       — patch loads, note-on produces non-silent audio.
 *   2. HPFFormant  — spectral centroid in 600–2000 Hz band (HPF removes bass,
 *                    LP caps top end — tighter window than English Horn).
 *   3. Audible     — ascending scale C4 → E4 → G4 → C5.
 */

#include <gtest/gtest.h>
#include "../TestHelper.hpp"
#include "PatchAnalysis.hpp"
#include <vector>
#include <cmath>
#include <thread>
#include <chrono>

class OboePatchTest : public ::testing::Test {
protected:
    void SetUp() override {
        test::init_test_environment();
        sample_rate = test::get_safe_sample_rate(0);
        engine_wrapper = std::make_unique<test::EngineWrapper>(sample_rate);
    }

    EngineHandle engine() { return engine_wrapper->get(); }

    int sample_rate;
    std::unique_ptr<test::EngineWrapper> engine_wrapper;

    static constexpr const char* kPatch = "patches/oboe.json";
};

// ---------------------------------------------------------------------------
// Test 1: Smoke
// ---------------------------------------------------------------------------

TEST_F(OboePatchTest, NoteOnProducesAudio) {
    PRINT_TEST_HEADER(
        "Oboe — Smoke",
        "Patch loads; note-on produces non-silent audio.",
        "engine_load_patch(oboe.json) → note_on(A4) → skip 3 blocks → measure 5",
        "RMS > 0.001",
        sample_rate
    );

    ASSERT_EQ(engine_load_patch(engine(), kPatch), 0) << "Failed to load " << kPatch;
    engine_note_on(engine(), 69, 1.0f);

    const size_t FRAMES = 512;
    std::vector<float> buf(FRAMES * 2);
    for (int b = 0; b < 3; ++b) engine_process(engine(), buf.data(), FRAMES);

    double sum_sq = 0.0;
    for (int b = 0; b < 5; ++b) {
        engine_process(engine(), buf.data(), FRAMES);
        for (size_t i = 0; i < FRAMES; ++i) sum_sq += double(buf[i * 2]) * double(buf[i * 2]);
    }
    engine_note_off(engine(), 69);

    float rms = float(std::sqrt(sum_sq / double(FRAMES * 5)));
    std::cout << "[Oboe] RMS (blocks 3–8): " << rms << "\n";
    EXPECT_GT(rms, 0.001f);
}

// ---------------------------------------------------------------------------
// Test 2: HPFFormant — centroid in 600–2000 Hz band
//
// HPF (300 Hz) removes bass; LP (2000 Hz, res=0.5) caps top end.
// For A4 (440 Hz), centroid should sit in the 600–2000 Hz nasal band —
// tighter window and brighter than English Horn (no HPF).
// ---------------------------------------------------------------------------

TEST_F(OboePatchTest, HPFFormant) {
    PRINT_TEST_HEADER(
        "Oboe — HPF Formant (automated)",
        "MOOG_FILTER LP 1200Hz res=0.92 + HPF 250Hz: centroid in 600–2000 Hz nasal band.",
        "engine_load_patch → note_on(A4) → skip 10 blocks → capture 2048 samples → centroid",
        "600 < centroid < 2000 Hz",
        sample_rate
    );

    ASSERT_EQ(engine_load_patch(engine(), kPatch), 0);
    engine_note_on(engine(), 69, 1.0f);

    const size_t FRAMES = 512;
    const size_t WINDOW = 2048;
    std::vector<float> buf(FRAMES * 2);
    std::vector<float> win;
    win.reserve(WINDOW);

    for (int b = 0; b < 10; ++b) engine_process(engine(), buf.data(), FRAMES);
    for (int b = 0; b < 6 && win.size() < WINDOW; ++b) {
        engine_process(engine(), buf.data(), FRAMES);
        for (size_t i = 0; i < FRAMES && win.size() < WINDOW; ++i)
            win.push_back(buf[i * 2]);
    }
    engine_note_off(engine(), 69);

    ASSERT_EQ(win.size(), WINDOW);
    float centroid = spectral_centroid(win, sample_rate);
    std::cout << "[Oboe] A4 fundamental:    440 Hz\n";
    std::cout << "[Oboe] Spectral centroid: " << centroid << " Hz\n";

    EXPECT_GT(centroid, 600.0f)  << "HPF (300 Hz) should push centroid above 600 Hz";
    EXPECT_LT(centroid, 2000.0f) << "LP (2000 Hz) should keep centroid below 2000 Hz";
}

// ---------------------------------------------------------------------------
// Test 3: Audible — C4 → E4 → G4 → C5
// ---------------------------------------------------------------------------

TEST_F(OboePatchTest, AscendingScaleAudible) {
    PRINT_TEST_HEADER(
        "Oboe — Ascending Scale (audible)",
        "Nasal, piercing double-reed timbre with HPF tightening the formant.",
        "engine_load_patch(oboe.json) → engine_start → C4/E4/G4/C5",
        "Audible oboe tone: brighter and more nasal than English Horn.",
        sample_rate
    );

    ASSERT_EQ(engine_load_patch(engine(), kPatch), 0);
    ASSERT_ENGINE_START(engine());

    constexpr int NOTE_MS    = 600;
    constexpr int RELEASE_MS = 300;

    const int notes[] = {60, 64, 67, 72};
    std::cout << "[Oboe] Playing C4 → E4 → G4 → C5…\n";
    for (int midi : notes) {
        engine_note_on(engine(), midi, 0.9f);
        std::this_thread::sleep_for(std::chrono::milliseconds(NOTE_MS));
        engine_note_off(engine(), midi);
        std::this_thread::sleep_for(std::chrono::milliseconds(RELEASE_MS));
    }

    engine_stop(engine());
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
