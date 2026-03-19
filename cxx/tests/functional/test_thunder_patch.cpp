/**
 * @file test_thunder_patch.cpp
 * @brief Functional tests for thunder.json — dual filtered-noise thunder rumble.
 *
 * Patch topology:
 *   WHITE_NOISE → MOOG_FILTER (cutoff=160, res=0.45) → AUDIO_MIXER.audio_in_1
 *   WHITE_NOISE → SH_FILTER   (cutoff=900, res=0.25) → AUDIO_MIXER.audio_in_2
 *   AUDIO_MIXER (gain_1=0.65, gain_2=0.35) → VCA ← ADSR
 *   ADSR: attack=0.8s, decay=0.2s, sustain=1.0, release=3.0s
 *
 * Characteristic features:
 *   - Two parallel noise paths: deep rumble (160 Hz Moog) + mid growl (900 Hz SH)
 *   - Very slow attack (800ms) — thunder rolls in gradually
 *   - Long release (3.0s) — thunder fades slowly after key-up
 *   - Sustain=1.0 means once fully on, it stays at full level
 *
 * Key assertions:
 *   1. NoteOnProducesAudio — skip 90 blocks (0.8s attack), measure 10 blocks, RMS > 0.001.
 *   2. SlowAttack          — blocks 5-10 (early) vs blocks 85-95 (near full): late > early * 1.5.
 *   3. LongSustain         — at block 120 (~1.28s), signal still > 0.001 RMS.
 *   4. ThunderRollAudible  — 5s rumble with 3s release fade.
 *
 * Timing at 48 kHz, block=512 (~10.67ms/block):
 *   attack=0.8s ≈ 75 blocks.
 *   Block 90 (~0.96s): near peak level.
 *   Block 120 (~1.28s): well into sustain, full amplitude.
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

class ThunderPatchTest : public ::testing::Test {
protected:
    void SetUp() override {
        test::init_test_environment();
        sample_rate = test::get_safe_sample_rate(0);
        engine_wrapper = std::make_unique<test::EngineWrapper>(sample_rate);
    }

    EngineHandle engine() { return engine_wrapper->get(); }

    int sample_rate;
    std::unique_ptr<test::EngineWrapper> engine_wrapper;

    static constexpr const char* kPatch = "patches/thunder.json";
    static constexpr size_t FRAMES = 512;
};

// ---------------------------------------------------------------------------
// Test 1: NoteOnProducesAudio — wait for slow attack, then measure audio
// ---------------------------------------------------------------------------

TEST_F(ThunderPatchTest, NoteOnProducesAudio) {
    PRINT_TEST_HEADER(
        "Thunder — Smoke (post-attack)",
        "After the 800ms attack builds, thunder produces non-silent audio.",
        "engine_load_patch(thunder.json) → note_on → skip 90 blocks → measure 10 blocks",
        "RMS > 0.001 after the 800ms attack has built up.",
        sample_rate
    );

    ASSERT_EQ(engine_load_patch(engine(), kPatch), 0);
    engine_note_on(engine(), 36, 1.0f);

    std::vector<float> buf(FRAMES * 2);

    // Skip 90 blocks (~0.96s) to get past the 800ms attack
    for (int b = 0; b < 90; ++b)
        engine_process(engine(), buf.data(), FRAMES);

    double sum_sq = 0.0;
    for (int b = 0; b < 10; ++b) {
        engine_process(engine(), buf.data(), FRAMES);
        for (size_t i = 0; i < FRAMES; ++i) sum_sq += double(buf[i * 2]) * double(buf[i * 2]);
    }
    engine_note_off(engine(), 36);

    float rms = float(std::sqrt(sum_sq / double(FRAMES * 10)));
    std::cout << "[Thunder] Post-attack RMS: " << rms << "\n";
    EXPECT_GT(rms, 0.001f) << "Expected non-silent thunder after 800ms attack builds";
}

// ---------------------------------------------------------------------------
// Test 2: SlowAttack — level grows dramatically over the 800ms attack
//
// EARLY window: blocks 5-10  (~53-107ms)  — attack barely started
// LATE  window: blocks 85-95 (~907-1014ms) — near full sustain level
// Expected: rms_late > rms_early * 1.5
// ---------------------------------------------------------------------------

TEST_F(ThunderPatchTest, SlowAttack) {
    PRINT_TEST_HEADER(
        "Thunder — Slow Attack Rise",
        "RMS at blocks 85-95 (~907ms) is >=1.5x the RMS at blocks 5-10 (~53ms), "
        "confirming the 800ms ADSR attack swell.",
        "note_on → capture early (blocks 5-10) and late (blocks 85-95) → compare",
        "rms_late > rms_early * 1.5",
        sample_rate
    );

    ASSERT_EQ(engine_load_patch(engine(), kPatch), 0);
    engine_note_on(engine(), 36, 1.0f);

    std::vector<float> buf(FRAMES * 2);
    double early_sq = 0.0; int early_n = 0;
    double late_sq  = 0.0; int late_n  = 0;

    const int EARLY_START = 5;
    const int EARLY_END   = 10;
    const int LATE_START  = 85;
    const int LATE_END    = 95;

    for (int b = 0; b < LATE_END; ++b) {
        engine_process(engine(), buf.data(), FRAMES);
        double block_sq = 0.0;
        for (size_t i = 0; i < FRAMES; ++i) block_sq += double(buf[i * 2]) * double(buf[i * 2]);
        if (b >= EARLY_START && b < EARLY_END) { early_sq += block_sq; ++early_n; }
        if (b >= LATE_START  && b < LATE_END)  { late_sq  += block_sq; ++late_n;  }
    }
    engine_note_off(engine(), 36);

    float rms_early = float(std::sqrt(early_sq / double(FRAMES * early_n)));
    float rms_late  = float(std::sqrt(late_sq  / double(FRAMES * late_n)));
    float ratio     = rms_early > 1e-6f ? rms_late / rms_early : 0.0f;

    std::cout << "[Thunder] RMS early (blocks 5-10):   " << rms_early << "\n";
    std::cout << "[Thunder] RMS late  (blocks 85-95):  " << rms_late  << "\n";
    std::cout << "[Thunder] Ratio late/early:           " << ratio     << "\n";

    EXPECT_GT(rms_late, rms_early * 1.5f)
        << "Expected RMS to grow by at least 1.5x during 800ms attack "
        << "(early=" << rms_early << " late=" << rms_late << ")";
}

// ---------------------------------------------------------------------------
// Test 3: LongSustain — signal present at block 120 (~1.28s)
//
// With sustain=1.0, once the attack completes the level stays at maximum.
// At block 120 (~1.28s), we are well into the sustain phase.
// ---------------------------------------------------------------------------

TEST_F(ThunderPatchTest, LongSustain) {
    PRINT_TEST_HEADER(
        "Thunder — Long Sustain",
        "With sustain=1.0, signal remains fully on at t≈1.28s (block 120).",
        "note_on → skip 120 blocks → measure 10 blocks → RMS > 0.001",
        "RMS > 0.001 at t≈1.28s (deep into sustain phase)",
        sample_rate
    );

    ASSERT_EQ(engine_load_patch(engine(), kPatch), 0);
    engine_note_on(engine(), 36, 1.0f);

    std::vector<float> buf(FRAMES * 2);

    for (int b = 0; b < 120; ++b)
        engine_process(engine(), buf.data(), FRAMES);

    double sum_sq = 0.0;
    for (int b = 0; b < 10; ++b) {
        engine_process(engine(), buf.data(), FRAMES);
        for (size_t i = 0; i < FRAMES; ++i) sum_sq += double(buf[i * 2]) * double(buf[i * 2]);
    }
    engine_note_off(engine(), 36);

    float rms = float(std::sqrt(sum_sq / double(FRAMES * 10)));
    std::cout << "[Thunder] Sustain RMS at t≈1.28s (block 120): " << rms << "\n";
    EXPECT_GT(rms, 0.001f) << "Expected full sustain level at t≈1.28s (sustain=1.0)";
}

// ---------------------------------------------------------------------------
// Test 4: ThunderRollAudible — 5s rumble with 3s release fade
// ---------------------------------------------------------------------------

TEST_F(ThunderPatchTest, ThunderRollAudible) {
    PRINT_TEST_HEADER(
        "Thunder — Thunder Roll (audible)",
        "Thunder note held 5 seconds (listen for slow 800ms attack build then full rumble), "
        "then released with 3-second fade.",
        "engine_load_patch(thunder.json) → engine_start → note_on → 5s → note_off → 4s",
        "Audible thunder roll: slow build, full low rumble, slow 3s fade.",
        sample_rate
    );

    ASSERT_EQ(engine_load_patch(engine(), kPatch), 0);
    ASSERT_ENGINE_START(engine());

    std::cout << "[Thunder] Rumbling for 5 seconds...\n";
    engine_note_on(engine(), 36, 1.0f);
    std::this_thread::sleep_for(std::chrono::milliseconds(5000));

    engine_note_off(engine(), 36);
    std::cout << "[Thunder] Fading out (3s release)...\n";
    std::this_thread::sleep_for(std::chrono::milliseconds(4000));

    engine_stop(engine());
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
