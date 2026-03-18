/**
 * @file test_organ_drawbar_patch.cpp
 * @brief Functional tests for organ_drawbar.json (Drawbar Organ, Full Ensemble).
 *
 * Patch topology:
 *   DRAWBAR_ORGAN (drawbars: 8'=8, 4'=8, 5⅓'=4, 2⅔'=3, 2'=2) → VCA ← ENV
 *   ENV: attack=5ms, decay=10ms, sustain=1.0, release=30ms
 *
 * Active harmonics (relative to C4 = 261.63 Hz):
 *   8'  (drawbar=8) → 1st harmonic:  261.63 Hz
 *   4'  (drawbar=8) → 2nd harmonic:  523.26 Hz
 *   5⅓' (drawbar=4) → 3rd harmonic:  784.89 Hz
 *   2⅔' (drawbar=3) → 3rd harmonic:  784.89 Hz  (reinforces)
 *   2'  (drawbar=2) → 4th harmonic: 1046.52 Hz
 *
 * Characteristic features:
 *   - Instant key-on: 5ms attack means full level by the second audio block
 *   - Constant volume while held: sustain=1.0, no envelope decay
 *   - Rich harmonic stack: multiple drawbars push spectral centroid well above
 *     the fundamental — for C4 (261 Hz), expected centroid ≈ 550+ Hz
 *
 * Tests:
 *   1. Smoke            — patch loads and note-on produces non-silent audio.
 *   2. InstantAttack    — RMS in the second block (~10–21ms) is ≥70% of steady-state
 *                         RMS, confirming the near-zero attack time.
 *   3. HarmonicCentroid — DCT spectral centroid of C4 during sustain is ≥1.8× the
 *                         fundamental frequency, confirming harmonic drawbar content.
 *   4. Audible          — play a two-octave organ scale (C4 to C6).
 */

#include <gtest/gtest.h>
#include "../TestHelper.hpp"
#include "PatchAnalysis.hpp"
#include <vector>
#include <cmath>
#include <thread>
#include <chrono>

// ---------------------------------------------------------------------------
// Fixture
// ---------------------------------------------------------------------------

class OrganDrawbarPatchTest : public ::testing::Test {
protected:
    void SetUp() override {
        test::init_test_environment();
        sample_rate = test::get_safe_sample_rate(0);
        engine_wrapper = std::make_unique<test::EngineWrapper>(sample_rate);
    }

    EngineHandle engine() { return engine_wrapper->get(); }

    int sample_rate;
    std::unique_ptr<test::EngineWrapper> engine_wrapper;

    static constexpr const char* kPatch = "patches/organ_drawbar.json";
};

// ---------------------------------------------------------------------------
// Test 1: Smoke — patch loads and produces audio
// ---------------------------------------------------------------------------

TEST_F(OrganDrawbarPatchTest, NoteOnProducesAudio) {
    PRINT_TEST_HEADER(
        "Organ — Smoke",
        "Note-on produces non-silent audio through DRAWBAR_ORGAN → VCA chain.",
        "engine_load_patch(organ_drawbar.json) → note_on → engine_process × 5",
        "RMS > 0.001 across 5 blocks.",
        sample_rate
    );

    ASSERT_EQ(engine_load_patch(engine(), kPatch), 0);
    engine_note_on(engine(), 60, 1.0f);  // C4

    const size_t FRAMES = 512;
    std::vector<float> buf(FRAMES * 2);
    double sum_sq = 0.0;
    for (int b = 0; b < 5; ++b) {
        engine_process(engine(), buf.data(), FRAMES);
        for (size_t i = 0; i < FRAMES; ++i) sum_sq += double(buf[i * 2]) * double(buf[i * 2]);
    }
    float rms = float(std::sqrt(sum_sq / double(FRAMES * 5)));
    std::cout << "[Organ] 5-block RMS: " << rms << "\n";
    EXPECT_GT(rms, 0.001f) << "Expected non-silent output at onset";

    engine_note_off(engine(), 60);
}

// ---------------------------------------------------------------------------
// Test 2: InstantAttack — organ is at near-full level by the second block
//
// attack=5ms, 1 block = 512/sr ≈ 10.67ms at 48kHz.
// The attack is complete before the end of block 0.
// Block 1 (10–21ms) should be ≥70% of the steady-state RMS at blocks 5–15.
// ---------------------------------------------------------------------------

TEST_F(OrganDrawbarPatchTest, InstantAttack) {
    PRINT_TEST_HEADER(
        "Organ — Instant Attack (automated)",
        "RMS in block 1 (~10–21ms) is ≥70% of steady-state RMS (blocks 5–15), "
        "confirming the 5ms organ key-click attack is essentially instantaneous.",
        "engine_load_patch → note_on(C4) → compare block-1 RMS vs steady-state",
        "rms_early / rms_steady ≥ 0.70",
        sample_rate
    );

    ASSERT_EQ(engine_load_patch(engine(), kPatch), 0);
    engine_note_on(engine(), 60, 1.0f);  // C4

    const size_t FRAMES = 512;
    std::vector<float> buf(FRAMES * 2);

    double early_sq = 0.0;   // block 1
    double steady_sq = 0.0;  // blocks 5–15
    int steady_n = 0;

    for (int b = 0; b < 16; ++b) {
        engine_process(engine(), buf.data(), FRAMES);
        double block_sq = 0.0;
        for (size_t i = 0; i < FRAMES; ++i) block_sq += double(buf[i * 2]) * double(buf[i * 2]);
        if (b == 1) early_sq = block_sq;
        if (b >= 5) { steady_sq += block_sq; ++steady_n; }
    }
    engine_note_off(engine(), 60);

    float rms_early  = float(std::sqrt(early_sq  / double(FRAMES)));
    float rms_steady = float(std::sqrt(steady_sq / double(FRAMES * steady_n)));
    float ratio      = rms_steady > 1e-6f ? rms_early / rms_steady : 0.0f;

    std::cout << "[Organ] RMS block-1 (~10–21ms):  " << rms_early  << "\n";
    std::cout << "[Organ] RMS steady (blocks 5–15): " << rms_steady << "\n";
    std::cout << "[Organ] Ratio early/steady:        " << ratio      << "\n";

    EXPECT_GT(rms_steady, 0.001f) << "No signal in steady window";
    EXPECT_GT(ratio, 0.70f)
        << "Expected ≥70% of steady-state RMS by block 1 (5ms attack); got " << ratio;
}

// ---------------------------------------------------------------------------
// Test 3: HarmonicCentroid — drawbars push spectral energy above the fundamental
//
// With drawbars at 8'=8, 4'=8, 5⅓'=4, 2⅔'=3, 2'=2, harmonics 1–4 are active.
// For C4 (261.63 Hz), the weighted centroid of the drawbar stack is ~550 Hz.
// A pure sine at C4 would give centroid ≈ 262 Hz.
// Threshold: centroid > fundamental × 1.8 (~471 Hz for C4).
// ---------------------------------------------------------------------------

TEST_F(OrganDrawbarPatchTest, HarmonicCentroid) {
    PRINT_TEST_HEADER(
        "Organ — Harmonic Drawbar Content (automated)",
        "Active drawbars (8'+4'+5⅓'+2⅔'+2') push DCT spectral centroid well above "
        "the C4 fundamental (261 Hz). Expected centroid ≈ 550+ Hz.",
        "engine_load_patch → note_on(C4) → skip 5 blocks → capture 2048 samples → centroid",
        "centroid > fundamental × 1.8 (~471 Hz)",
        sample_rate
    );

    ASSERT_EQ(engine_load_patch(engine(), kPatch), 0);
    engine_note_on(engine(), 60, 1.0f);  // C4 = 261.63 Hz

    const size_t BLOCK  = 512;
    const size_t WINDOW = 2048;
    std::vector<float> buf(BLOCK * 2);
    std::vector<float> mono;
    mono.reserve(WINDOW);

    // Skip 5 blocks (attack complete), then capture WINDOW samples
    for (int b = 0; b < 10; ++b) {
        engine_process(engine(), buf.data(), BLOCK);
        if (b >= 5) {
            for (size_t i = 0; i < BLOCK && mono.size() < WINDOW; ++i)
                mono.push_back(buf[i * 2]);
        }
    }
    engine_note_off(engine(), 60);

    ASSERT_EQ(mono.size(), WINDOW);

    constexpr float kC4Hz = 261.63f;
    float centroid = spectral_centroid(mono, sample_rate);

    std::cout << "[Organ] C4 fundamental:    " << kC4Hz    << " Hz\n";
    std::cout << "[Organ] Spectral centroid:  " << centroid << " Hz\n";
    std::cout << "[Organ] Centroid / fund.:   " << centroid / kC4Hz << "×\n";

    EXPECT_GT(centroid, kC4Hz * 1.8f)
        << "Expected centroid > " << kC4Hz * 1.8f << " Hz from drawbar harmonics; "
        << "got " << centroid << " Hz";
}

// ---------------------------------------------------------------------------
// Test 4: Audible — two-octave organ scale
// ---------------------------------------------------------------------------

TEST_F(OrganDrawbarPatchTest, OrganScaleAudible) {
    PRINT_TEST_HEADER(
        "Organ — Two-Octave Scale (audible)",
        "Play C4 to C6 (two octaves) to hear the drawbar harmonic character, "
        "instant key-click, and clean organ sustain.",
        "engine_load_patch(organ_drawbar.json) → engine_start → C4–C6 scale",
        "Audible drawbar organ with bright harmonic content and crisp key-click.",
        sample_rate
    );

    ASSERT_EQ(engine_load_patch(engine(), kPatch), 0);
    ASSERT_EQ(engine_start(engine()), 0);

    constexpr int NOTE_MS    = 200;
    constexpr int RELEASE_MS =  80;  // 30ms release + margin

    std::cout << "[Organ] Playing C4–C6 two-octave scale…\n";
    for (int midi = 60; midi <= 84; ++midi) {  // C4 to C6
        engine_note_on(engine(), midi, 1.0f);
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
