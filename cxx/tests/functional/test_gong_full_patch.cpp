/**
 * @file test_gong_full_patch.cpp
 * @brief Functional tests for gong_full.json — ring-mod gong with noise impact layer.
 *
 * Patch topology:
 *   VCO1 (sine) → RING_MOD.audio_in_a
 *   VCO2 (sine, transpose=+7, detune=40) → RING_MOD.audio_in_b
 *   WHITE_NOISE → MOOG_FILTER (cutoff=5000, res=0.2) → AUDIO_MIXER.audio_in_2
 *   RING_MOD.audio_out → AUDIO_MIXER.audio_in_1
 *   AUDIO_MIXER (gain_1=0.8, gain_2=0.2) → VCA ← AD_ENVELOPE (attack=1ms, decay=3500ms)
 *
 * The noise layer through a high-pass Moog filter creates the percussive impact
 * transient of a struck gong, while the ring modulator provides the long inharmonic
 * shimmer of the metal body.
 *
 * Key assertions:
 *   1. NoteOnProducesAudio  — first 5 blocks produce non-silent output.
 *   2. NoiseTransientPresent — onset RMS > 0.001; ring RMS (blocks 50-55) > 0.0005.
 *   3. LongDecay             — signal persists at t≈1s (block 95-100), RMS > 0.001.
 *   4. SingleStrikeAudible   — C2 strike with 4s ring-out.
 *
 * Timing at 48 kHz, block=512 (~10.67ms/block):
 *   decay=3500ms ≈ 328 blocks.
 *   At block 94 (~1.0s): envelope ≈ e^(-1.0/3.5) ≈ 0.75 of peak.
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

class GongFullPatchTest : public ::testing::Test {
protected:
    void SetUp() override {
        test::init_test_environment();
        sample_rate = test::get_safe_sample_rate(0);
        engine_wrapper = std::make_unique<test::EngineWrapper>(sample_rate);
    }

    EngineHandle engine() { return engine_wrapper->get(); }

    int sample_rate;
    std::unique_ptr<test::EngineWrapper> engine_wrapper;

    static constexpr const char* kPatch = "patches/gong_full.json";
    static constexpr size_t FRAMES = 512;
};

// ---------------------------------------------------------------------------
// Test 1: NoteOnProducesAudio — ring-mod + noise produces audio at onset
// ---------------------------------------------------------------------------

TEST_F(GongFullPatchTest, NoteOnProducesAudio) {
    PRINT_TEST_HEADER(
        "Gong Full — Smoke",
        "Note-on(A4) produces non-silent audio: noise transient + ring-mod body.",
        "engine_load_patch(gong_full.json) → note_on(A4=69) → engine_process × 5",
        "RMS > 0.001 within first 5 blocks.",
        sample_rate
    );

    ASSERT_EQ(engine_load_patch(engine(), kPatch), 0);
    engine_note_on(engine(), 69, 1.0f);  // A4

    std::vector<float> buf(FRAMES * 2);
    double sum_sq = 0.0;
    for (int b = 0; b < 5; ++b) {
        engine_process(engine(), buf.data(), FRAMES);
        for (size_t i = 0; i < FRAMES; ++i) sum_sq += double(buf[i * 2]) * double(buf[i * 2]);
    }
    float rms = float(std::sqrt(sum_sq / double(FRAMES * 5)));
    std::cout << "[GongFull] Onset RMS (5 blocks): " << rms << "\n";
    EXPECT_GT(rms, 0.001f) << "Expected non-silent output at gong onset";

    engine_note_off(engine(), 69);
}

// ---------------------------------------------------------------------------
// Test 2: NoiseTransientPresent — onset and sustained ring both measurable
//
// onset window:  blocks 0-3  (~0-32ms)   — noise + ring-mod peak
// ring window:   blocks 50-55 (~533-586ms) — noise component mostly decayed
//                                            but ring-mod body still audible
// Both windows should be non-silent; onset prints both values.
// ---------------------------------------------------------------------------

TEST_F(GongFullPatchTest, NoiseTransientPresent) {
    PRINT_TEST_HEADER(
        "Gong Full — Noise Transient + Ring Body",
        "Both the noise impact onset and sustained ring-mod body are audible. "
        "Onset (blocks 0-3) dominated by noise transient; "
        "ring phase (blocks 50-55) still carries energy from decay=3.5s envelope.",
        "note_on(A4) → capture onset blocks 0-3 and ring blocks 50-55",
        "onset_rms > 0.001, ring_rms > 0.0005",
        sample_rate
    );

    ASSERT_EQ(engine_load_patch(engine(), kPatch), 0);
    engine_note_on(engine(), 69, 1.0f);  // A4

    std::vector<float> buf(FRAMES * 2);
    double onset_sq = 0.0; int onset_n = 0;
    double ring_sq  = 0.0; int ring_n  = 0;

    const int ONSET_START = 0;
    const int ONSET_END   = 3;
    const int RING_START  = 50;
    const int RING_END    = 55;

    for (int b = 0; b < RING_END; ++b) {
        engine_process(engine(), buf.data(), FRAMES);
        double block_sq = 0.0;
        for (size_t i = 0; i < FRAMES; ++i) block_sq += double(buf[i * 2]) * double(buf[i * 2]);
        if (b >= ONSET_START && b < ONSET_END) { onset_sq += block_sq; ++onset_n; }
        if (b >= RING_START  && b < RING_END)  { ring_sq  += block_sq; ++ring_n;  }
    }
    engine_note_off(engine(), 69);

    float onset_rms = float(std::sqrt(onset_sq / double(FRAMES * onset_n)));
    float ring_rms  = float(std::sqrt(ring_sq  / double(FRAMES * ring_n)));

    std::cout << "[GongFull] Onset RMS (blocks 0-3):   " << onset_rms << "\n";
    std::cout << "[GongFull] Ring RMS  (blocks 50-55): " << ring_rms  << "\n";

    EXPECT_GT(onset_rms, 0.001f)  << "Expected non-silent onset (noise + ring-mod)";
    EXPECT_GT(ring_rms,  0.0005f) << "Expected ring-mod body to still carry energy at ~530ms";
}

// ---------------------------------------------------------------------------
// Test 3: LongDecay — signal persists at t≈1s (blocks 95-100)
//
// At 48kHz, block 95 ≈ 1.01s. AD_ENVELOPE decay=3.5s:
//   level at 1s ≈ exp(-1.0/3.5) ≈ 0.75 of peak — clearly audible.
// ---------------------------------------------------------------------------

TEST_F(GongFullPatchTest, LongDecay) {
    PRINT_TEST_HEADER(
        "Gong Full — Long Decay (automated)",
        "AD decay=3500ms: signal remains clearly audible at t≈1.07s (blocks 95-100).",
        "engine_load_patch → note_on(A4) → skip 95 blocks → measure 5 blocks",
        "RMS > 0.001 at t≈1.0s (decay ~75% of peak)",
        sample_rate
    );

    ASSERT_EQ(engine_load_patch(engine(), kPatch), 0);
    engine_note_on(engine(), 69, 1.0f);  // A4

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
    std::cout << "[GongFull] RMS at t≈1.0s (blocks 95-100): " << rms << "\n";
    EXPECT_GT(rms, 0.001f) << "Expected gong to remain audible at 1 second (decay=3.5s)";
}

// ---------------------------------------------------------------------------
// Test 4: SingleStrikeAudible — single gong strike with full ring-out
// ---------------------------------------------------------------------------

TEST_F(GongFullPatchTest, SingleStrikeAudible) {
    PRINT_TEST_HEADER(
        "Gong Full — Single Strike (audible)",
        "Single C2 gong strike with 4s ring-out. "
        "Hear the noise impact transient followed by long inharmonic shimmer.",
        "engine_load_patch(gong_full.json) → engine_start → C2 strike → 4s ring-out",
        "Audible deep gong with noise attack and long shimmer decay.",
        sample_rate
    );

    ASSERT_EQ(engine_load_patch(engine(), kPatch), 0);
    ASSERT_ENGINE_START(engine());

    std::cout << "[GongFull] Striking C2 — listening for 4s ring-out...\n";
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
