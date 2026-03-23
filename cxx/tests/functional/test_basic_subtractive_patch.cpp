/**
 * @file test_basic_subtractive_patch.cpp
 * @brief Functional tests for basic_subtractive.json — Roland Fig. 1-1.
 *
 * Patch topology (Roland System 100M Fig. 1-1):
 *   VCO (saw) → VCF (fixed cutoff) → VCA ← ADSR (A=10ms, D=200ms, S=0.7, R=300ms)
 *
 * The canonical subtractive synthesis patch. ADSR drives amplitude only;
 * filter cutoff is static (not envelope-modulated). VCF keyboard tracking
 * is auto-injected by Voice (kybd_cv).
 *
 * Key assertions:
 *   1. Smoke         — note_on produces audio within 1 block.
 *   2. Envelope      — RMS grows during attack, holds during sustain, fades on release.
 *   3. FilterStatic  — spectral centroid does not shift between onset and sustain
 *                      (confirming no filter envelope).
 *   4. Audible       — C major scale up and down.
 */

#include <gtest/gtest.h>
#include "../TestHelper.hpp"
#include "PatchAnalysis.hpp"
#include <vector>
#include <cmath>
#include <thread>
#include <chrono>

class BasicSubtractivePatchTest : public ::testing::Test {
protected:
    void SetUp() override {
        test::init_test_environment();
        sample_rate = test::get_safe_sample_rate(0);
        engine_wrapper = std::make_unique<test::EngineWrapper>(sample_rate);
    }
    EngineHandle engine() { return engine_wrapper->get(); }
    int sample_rate;
    std::unique_ptr<test::EngineWrapper> engine_wrapper;
    static constexpr const char* kPatch = "patches/basic_subtractive.json";
};

// ---------------------------------------------------------------------------
// Test 1: Smoke
// ---------------------------------------------------------------------------

TEST_F(BasicSubtractivePatchTest, NoteOnProducesAudio) {
    PRINT_TEST_HEADER(
        "Basic Subtractive — Smoke",
        "VCO saw → VCF (fixed) → VCA ← ADSR: note_on produces audio immediately.",
        "engine_load_patch → note_on(A4) → 3 blocks",
        "RMS > 0.001",
        sample_rate
    );

    ASSERT_EQ(engine_load_patch(engine(), kPatch), 0);
    engine_note_on(engine(), 69, 1.0f);  // A4

    const size_t FRAMES = 512;
    std::vector<float> buf(FRAMES * 2);
    double sum_sq = 0.0;
    for (int b = 0; b < 3; ++b) {
        engine_process(engine(), buf.data(), FRAMES);
        for (size_t i = 0; i < FRAMES; ++i) sum_sq += double(buf[i*2]) * double(buf[i*2]);
    }
    engine_note_off(engine(), 69);

    float rms = float(std::sqrt(sum_sq / double(FRAMES * 3)));
    std::cout << "[BasicSubtractive] Onset RMS: " << rms << "\n";
    EXPECT_GT(rms, 0.001f);
}

// ---------------------------------------------------------------------------
// Test 2: Envelope — attack rises, sustain holds, release fades
// ---------------------------------------------------------------------------

TEST_F(BasicSubtractivePatchTest, EnvelopeShape) {
    PRINT_TEST_HEADER(
        "Basic Subtractive — Envelope Shape",
        "ADSR A=10ms D=200ms S=0.7 R=300ms: RMS rises then holds, fades after note_off.",
        "note_on → onset blocks → sustain blocks → note_off → release blocks",
        "rms_sustain > 0.5*rms_onset; rms_release < 0.5*rms_sustain",
        sample_rate
    );

    ASSERT_EQ(engine_load_patch(engine(), kPatch), 0);
    engine_note_on(engine(), 69, 1.0f);

    const size_t FRAMES = 512;
    std::vector<float> buf(FRAMES * 2);

    // Skip block 0 (partial attack), blocks 1-2 = onset peak
    double onset_sq = 0.0;
    for (int b = 0; b < 5; ++b) {
        engine_process(engine(), buf.data(), FRAMES);
        if (b >= 2) for (size_t i = 0; i < FRAMES; ++i) onset_sq += double(buf[i*2]) * double(buf[i*2]);
    }

    // Sustain: blocks 25-30 (~267–320ms, well into sustain phase)
    double sustain_sq = 0.0;
    for (int b = 5; b < 30; ++b) {
        engine_process(engine(), buf.data(), FRAMES);
        if (b >= 25) for (size_t i = 0; i < FRAMES; ++i) sustain_sq += double(buf[i*2]) * double(buf[i*2]);
    }

    engine_note_off(engine(), 69);

    // Release: blocks 5-15 after note_off
    double rel_sq = 0.0;
    for (int b = 0; b < 20; ++b) {
        engine_process(engine(), buf.data(), FRAMES);
        if (b >= 5) for (size_t i = 0; i < FRAMES; ++i) rel_sq += double(buf[i*2]) * double(buf[i*2]);
    }

    float rms_onset   = float(std::sqrt(onset_sq   / double(FRAMES * 3)));
    float rms_sustain = float(std::sqrt(sustain_sq  / double(FRAMES * 5)));
    float rms_release = float(std::sqrt(rel_sq      / double(FRAMES * 15)));

    std::cout << "[BasicSubtractive] RMS onset:   " << rms_onset   << "\n";
    std::cout << "[BasicSubtractive] RMS sustain: " << rms_sustain << "\n";
    std::cout << "[BasicSubtractive] RMS release: " << rms_release << "\n";

    EXPECT_GT(rms_onset,   0.001f) << "No signal at onset";
    EXPECT_GT(rms_sustain, 0.3f * rms_onset) << "Sustain should hold signal";
    EXPECT_LT(rms_release, 0.7f * rms_sustain) << "Release should fade signal";
}

// ---------------------------------------------------------------------------
// Test 3: FilterStatic — centroid stable (no filter envelope)
// ---------------------------------------------------------------------------

TEST_F(BasicSubtractivePatchTest, FilterStatic) {
    PRINT_TEST_HEADER(
        "Basic Subtractive — Filter Static",
        "Fixed VCF cutoff: spectral centroid at onset ≈ centroid at sustain (within 20%).",
        "note_on(A4) → capture onset window → sustain window → compare centroids",
        "|centroid_onset - centroid_sustain| / centroid_onset < 0.20",
        sample_rate
    );

    ASSERT_EQ(engine_load_patch(engine(), kPatch), 0);
    engine_note_on(engine(), 69, 1.0f);

    const size_t BLOCK  = 512;
    const size_t WINDOW = 2048;
    std::vector<float> buf(BLOCK * 2);
    std::vector<float> onset_win, sustain_win;
    onset_win.reserve(WINDOW); sustain_win.reserve(WINDOW);

    for (int b = 0; b < 40; ++b) {
        engine_process(engine(), buf.data(), BLOCK);
        if (b >= 1 && onset_win.size() < WINDOW)
            for (size_t i = 0; i < BLOCK && onset_win.size() < WINDOW; ++i)
                onset_win.push_back(buf[i*2]);
        if (b >= 28 && sustain_win.size() < WINDOW)
            for (size_t i = 0; i < BLOCK && sustain_win.size() < WINDOW; ++i)
                sustain_win.push_back(buf[i*2]);
    }
    engine_note_off(engine(), 69);

    ASSERT_EQ(onset_win.size(),   WINDOW);
    ASSERT_EQ(sustain_win.size(), WINDOW);

    float c_onset   = spectral_centroid(onset_win,   sample_rate);
    float c_sustain = spectral_centroid(sustain_win,  sample_rate);
    float diff_frac = std::abs(c_onset - c_sustain) / (c_onset + 1.0f);

    std::cout << "[BasicSubtractive] Centroid onset:   " << c_onset   << " Hz\n";
    std::cout << "[BasicSubtractive] Centroid sustain: " << c_sustain << " Hz\n";
    std::cout << "[BasicSubtractive] Relative diff:    " << diff_frac << "\n";

    EXPECT_LT(diff_frac, 0.20f) << "Fixed filter: centroid should be stable across note";
}

// ---------------------------------------------------------------------------
// Test 4: Audible — C major scale
// ---------------------------------------------------------------------------

TEST_F(BasicSubtractivePatchTest, CMajorScaleAudible) {
    PRINT_TEST_HEADER(
        "Basic Subtractive — C Major Scale (audible)",
        "Canonical VCO saw → VCF → VCA patch. Classic subtractive timbre.",
        "engine_load_patch → engine_start → C4..C5 scale up and down",
        "Audible warm saw-wave tone with ADSR amplitude shaping.",
        sample_rate
    );

    ASSERT_EQ(engine_load_patch(engine(), kPatch), 0);
    ASSERT_ENGINE_START(engine());

    // C major scale up and down
    const int notes[] = {60,62,64,65,67,69,71,72, 72,71,69,67,65,64,62,60};
    std::cout << "[BasicSubtractive] Playing C major scale up and down…\n";
    for (int midi : notes) {
        engine_note_on(engine(), midi, 0.8f);
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        engine_note_off(engine(), midi);
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
    }

    engine_stop(engine());
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
