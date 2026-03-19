/**
 * @file test_gong_patch.cpp
 * @brief Functional tests for gong.json — ring modulator with perfect-5th interval.
 *
 * Patch topology (Practical Synthesis Vol. 2, Fig 3-17 gong approximation):
 *   VCO1 (sine) → RING_MOD.audio_in_a
 *   VCO2 (sine, transpose=+7 semitones + detune=+40 cents) → RING_MOD.audio_in_b
 *   RING_MOD → VCA ← AD_ENVELOPE (attack=1ms, decay=3500ms)
 *
 * Ring modulation between a fundamental and a detuned perfect 5th creates
 * inharmonic sidebands: for A4 (440 Hz) with VCO2 at ~687 Hz,
 * sidebands are at sum=1127 Hz and diff=247 Hz. The 3.5s decay time
 * produces the long, shimmering ring-out characteristic of a large gong.
 *
 * Key assertions:
 *   1. Smoke       — note_on produces non-silent audio.
 *   2. LongDecay   — signal persists well past 1 second (decay=3.5s).
 *   3. Inharmonic  — centroid deviates from VCO1 fundamental (sideband content).
 *   4. Audible     — single low strike (C2) with full 3.5s ring-out.
 *
 * Timing at 48 kHz, block=512 (~10.67ms/block):
 *   decay=3500ms ≈ 328 blocks.
 *   At block 94 (~1.0s): envelope ≈ e^(−1.0/3.5) ≈ 0.75 of peak.
 *   Signal should be clearly audible at 1 second.
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

class GongPatchTest : public ::testing::Test {
protected:
    void SetUp() override {
        test::init_test_environment();
        sample_rate = test::get_safe_sample_rate(0);
        engine_wrapper = std::make_unique<test::EngineWrapper>(sample_rate);
    }

    EngineHandle engine() { return engine_wrapper->get(); }

    int sample_rate;
    std::unique_ptr<test::EngineWrapper> engine_wrapper;

    static constexpr const char* kPatch = "patches/gong.json";
};

// ---------------------------------------------------------------------------
// Test 1: Smoke — ring-mod produces non-silent audio
// ---------------------------------------------------------------------------

TEST_F(GongPatchTest, NoteOnProducesAudio) {
    PRINT_TEST_HEADER(
        "Gong — Smoke",
        "Ring modulator (VCO1 × VCO2 at +7st) with AD envelope produces non-silent audio.",
        "engine_load_patch(gong.json) → note_on(A4) → engine_process × 5",
        "RMS > 0.001 within first 5 blocks.",
        sample_rate
    );

    ASSERT_EQ(engine_load_patch(engine(), kPatch), 0);
    engine_note_on(engine(), 69, 1.0f);  // A4

    const size_t FRAMES = 512;
    std::vector<float> buf(FRAMES * 2);
    double sum_sq = 0.0;
    for (int b = 0; b < 5; ++b) {
        engine_process(engine(), buf.data(), FRAMES);
        for (size_t i = 0; i < FRAMES; ++i) sum_sq += double(buf[i * 2]) * double(buf[i * 2]);
    }
    float rms = float(std::sqrt(sum_sq / double(FRAMES * 5)));
    std::cout << "[Gong] Onset RMS: " << rms << "\n";
    EXPECT_GT(rms, 0.001f) << "Expected non-silent ring-mod output";

    engine_note_off(engine(), 69);
}

// ---------------------------------------------------------------------------
// Test 2: LongDecay — signal persists well past 1 second (decay=3.5s)
//
// At t=1.0s (block 94), envelope ≈ 0.75 of peak amplitude.
// The gong should still be clearly audible.
// ---------------------------------------------------------------------------

TEST_F(GongPatchTest, LongDecay) {
    PRINT_TEST_HEADER(
        "Gong — Long Decay (automated)",
        "AD decay=3500ms: signal remains well above noise floor at t≈1.07s (block 100).",
        "engine_load_patch → note_on(A4) → skip 95 blocks → measure 5 blocks",
        "RMS > 0.001 at t≈1.0s (still in decay, ~75% of peak)",
        sample_rate
    );

    ASSERT_EQ(engine_load_patch(engine(), kPatch), 0);
    engine_note_on(engine(), 69, 1.0f);  // A4

    const size_t FRAMES = 512;
    std::vector<float> buf(FRAMES * 2);

    for (int b = 0; b < 95; ++b)
        engine_process(engine(), buf.data(), FRAMES);

    double sum_sq = 0.0;
    for (int b = 0; b < 5; ++b) {
        engine_process(engine(), buf.data(), FRAMES);
        for (size_t i = 0; i < FRAMES; ++i) sum_sq += double(buf[i * 2]) * double(buf[i * 2]);
    }
    engine_note_off(engine(), 69);

    float rms = float(std::sqrt(sum_sq / double(FRAMES * 5)));
    std::cout << "[Gong] RMS at t≈1.0s: " << rms << " (expected ~0.75× onset level)\n";
    EXPECT_GT(rms, 0.001f)
        << "Expected gong to remain audible at 1 second (decay=3.5s)";
}

// ---------------------------------------------------------------------------
// Test 3: Inharmonic — centroid > fundamental × 1.2 (sideband content)
//
// For A4 (440 Hz) with VCO2 at transpose=7 (+ detune ≈ 2 Hz):
//   VCO2 ≈ 440 × 2^(7.4/12) ≈ 440 × 1.566 ≈ 689 Hz
//   Sidebands: sum = 440 + 689 = 1129 Hz, diff = 689 − 440 = 249 Hz
//   Centroid skewed by high-energy sum and diff components above the fundamental.
// A pure 440 Hz sine gives centroid ≈ 440 Hz.
// Expected: centroid > 440 × 1.2 = 528 Hz.
// ---------------------------------------------------------------------------

TEST_F(GongPatchTest, InharmonicSidebands) {
    PRINT_TEST_HEADER(
        "Gong — Inharmonic Sidebands (automated)",
        "Ring-mod output centroid > 1.2× VCO1 fundamental (A4=440 Hz), "
        "confirming inharmonic sideband content rather than a pure fundamental.",
        "engine_load_patch → note_on(A4) → capture 2048 samples → centroid",
        "centroid > 440 × 1.2 = 528 Hz",
        sample_rate
    );

    ASSERT_EQ(engine_load_patch(engine(), kPatch), 0);
    engine_note_on(engine(), 69, 1.0f);  // A4 = 440 Hz

    const size_t BLOCK  = 512;
    const size_t WINDOW = 2048;
    std::vector<float> buf(BLOCK * 2);
    std::vector<float> mono;
    mono.reserve(WINDOW);

    for (int b = 0; b < 7; ++b) {
        engine_process(engine(), buf.data(), BLOCK);
        if (b >= 2 && mono.size() < WINDOW) {
            for (size_t i = 0; i < BLOCK && mono.size() < WINDOW; ++i)
                mono.push_back(buf[i * 2]);
        }
    }
    engine_note_off(engine(), 69);

    ASSERT_EQ(mono.size(), WINDOW);

    constexpr float kA4Hz = 440.0f;
    float centroid = spectral_centroid(mono, sample_rate);

    std::cout << "[Gong] A4 fundamental:    " << kA4Hz    << " Hz\n";
    std::cout << "[Gong] Spectral centroid: " << centroid << " Hz\n";
    std::cout << "[Gong] Centroid / fund.:  " << centroid / kA4Hz << "×\n";

    EXPECT_GT(centroid, kA4Hz * 1.2f)
        << "Expected centroid > " << kA4Hz * 1.2f << " Hz (ring-mod sidebands); "
        << "got " << centroid << " Hz";
}

// ---------------------------------------------------------------------------
// Test 4: Audible — single low gong strike with full ring-out
// ---------------------------------------------------------------------------

TEST_F(GongPatchTest, SingleStrikeAudible) {
    PRINT_TEST_HEADER(
        "Gong — Single Strike (audible)",
        "Single low gong strike (C2) with 3.5s decay ring-out. "
        "Hear the inharmonic shimmering quality from the ring modulator.",
        "engine_load_patch(gong.json) → engine_start → C2 strike → ring out",
        "Audible deep inharmonic gong with long shimmer decay.",
        sample_rate
    );

    ASSERT_EQ(engine_load_patch(engine(), kPatch), 0);
    ASSERT_ENGINE_START(engine());

    std::cout << "[Gong] Striking C2 — listening for 4s ring-out…\n";
    engine_note_on(engine(), 36, 1.0f);  // C2 — deep gong
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    engine_note_off(engine(), 36);

    // Let the 3.5s decay ring out
    std::this_thread::sleep_for(std::chrono::milliseconds(4000));
    engine_stop(engine());
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
