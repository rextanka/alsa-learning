/**
 * @file test_group_strings_patch.cpp
 * @brief Functional tests for group_strings.json — dual-VCO ensemble strings.
 *
 * Patch topology (Phase 26 rework — dual-VCO via AUDIO_MIXER):
 *   VCO1 (saw=1.0, sub=0.15) → AUDIO_MIXER.audio_in_1
 *   VCO2 (saw=1.0, sub=0.15, transpose=7) → AUDIO_MIXER.audio_in_2
 *   AUDIO_MIXER (gain_1=0.65, gain_2=0.65) → MOOG_FILTER (cutoff=2400 Hz, res=0.06)
 *       → VCA ← ADSR_ENVELOPE (attack=0.35s, decay=0.1s, sustain=0.85, release=0.5s)
 *   LFO (0.35 Hz, intensity=0.002) → VCO1.pitch_cv + VCO2.pitch_cv  (ensemble shimmer)
 *
 * Roland's group strings patch is characterised by the slow bow-attack (§4-6
 * "Strings" entry) and a warm, open filter. The dual-VCO detuning (7 cents)
 * and shared LFO vibrato produce the gentle pitch instability that distinguishes
 * a string section from a single voice.
 *
 * Key assertions:
 *   1. Smoke       — note_on + 60 warm-up blocks produces non-silent audio.
 *   2. SlowAttack  — RMS at blocks 2–5 (20–53ms) < RMS at blocks 38–42 (400ms).
 *   3. LongSustain — RMS stays above threshold well past the attack plateau.
 *   4. Audible     — slow G major chord melody (G3 / B3 / D4 / G4).
 *
 * Timing at 48 kHz, block=512 (~10.67ms/block):
 *   attack=0.35s ≈ 32.8 blocks.
 *   Blocks 2–5:   ~20–53ms   (early attack, ≈3% of plateau amplitude)
 *   Blocks 38–42: ~405–448ms (attack complete, sustain level)
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

class GroupStringsPatchTest : public ::testing::Test {
protected:
    void SetUp() override {
        test::init_test_environment();
        sample_rate = test::get_safe_sample_rate(0);
        engine_wrapper = std::make_unique<test::EngineWrapper>(sample_rate);
    }

    EngineHandle engine() { return engine_wrapper->get(); }

    int sample_rate;
    std::unique_ptr<test::EngineWrapper> engine_wrapper;

    static constexpr const char* kPatch = "patches/group_strings.json";
};

// ---------------------------------------------------------------------------
// Test 1: Smoke — produces audio after attack has partially opened (block 50+)
// ---------------------------------------------------------------------------

TEST_F(GroupStringsPatchTest, NoteOnProducesAudio) {
    PRINT_TEST_HEADER(
        "Group Strings — Smoke",
        "COMPOSITE_GENERATOR + MOOG_FILTER + ADSR: non-silent after attack.",
        "engine_load_patch(group_strings.json) → note_on(C4) → skip 40 blocks → measure 10",
        "RMS > 0.001 after attack completes (~430ms).",
        sample_rate
    );

    ASSERT_EQ(engine_load_patch(engine(), kPatch), 0);
    engine_note_on(engine(), 60, 1.0f);  // C4

    const size_t FRAMES = 512;
    std::vector<float> buf(FRAMES * 2);

    for (int b = 0; b < 40; ++b)
        engine_process(engine(), buf.data(), FRAMES);

    double sum_sq = 0.0;
    for (int b = 0; b < 10; ++b) {
        engine_process(engine(), buf.data(), FRAMES);
        for (size_t i = 0; i < FRAMES; ++i) sum_sq += double(buf[i * 2]) * double(buf[i * 2]);
    }
    engine_note_off(engine(), 60);

    float rms = float(std::sqrt(sum_sq / double(FRAMES * 10)));
    std::cout << "[GroupStrings] RMS after attack: " << rms << "\n";
    EXPECT_GT(rms, 0.001f) << "Expected sustained strings audio after attack";
}

// ---------------------------------------------------------------------------
// Test 2: SlowAttack — RMS grows from early (blocks 2–5) to late (blocks 38–42)
//
// attack=0.35s ≈ 32.8 blocks.  IIR attack reaches 99% at 0.35s.
// At 53ms (block 5) envelope ≈ 1 − e^(−log99 × 0.053/0.35) ≈ 48% of plateau.
// At 448ms (block 42) envelope ≈ 100% (plateau).
// Expected: rms_late > rms_early × 1.5
// ---------------------------------------------------------------------------

TEST_F(GroupStringsPatchTest, SlowAttack) {
    PRINT_TEST_HEADER(
        "Group Strings — Slow Attack (automated)",
        "ADSR attack=0.35s: RMS at ~50ms should be noticeably less than RMS at ~440ms.",
        "engine_load_patch → note_on(C4) → 43 blocks → compare early vs late window RMS",
        "rms_late > rms_early × 1.5",
        sample_rate
    );

    ASSERT_EQ(engine_load_patch(engine(), kPatch), 0);
    engine_note_on(engine(), 60, 1.0f);  // C4

    const size_t FRAMES      = 512;
    const int    EARLY_START = 2;
    const int    EARLY_END   = 6;   // ~20–64ms
    const int    LATE_START  = 37;
    const int    LATE_END    = 43;  // ~394–458ms

    std::vector<float> buf(FRAMES * 2);
    double early_sq = 0.0; int early_n = 0;
    double late_sq  = 0.0; int late_n  = 0;

    for (int b = 0; b < LATE_END; ++b) {
        engine_process(engine(), buf.data(), FRAMES);
        double block_sq = 0.0;
        for (size_t i = 0; i < FRAMES; ++i) block_sq += double(buf[i * 2]) * double(buf[i * 2]);
        if (b >= EARLY_START && b < EARLY_END) { early_sq += block_sq; ++early_n; }
        if (b >= LATE_START  && b < LATE_END)  { late_sq  += block_sq; ++late_n;  }
    }
    engine_note_off(engine(), 60);

    float rms_early = float(std::sqrt(early_sq / double(FRAMES * early_n)));
    float rms_late  = float(std::sqrt(late_sq  / double(FRAMES * late_n)));

    std::cout << "[GroupStrings] RMS early (~20–64ms):   " << rms_early << "\n";
    std::cout << "[GroupStrings] RMS late  (~394–458ms): " << rms_late  << "\n";

    EXPECT_GT(rms_late, rms_early * 1.5f)
        << "Expected late RMS > 1.5× early RMS for attack=0.35s bow attack";
}

// ---------------------------------------------------------------------------
// Test 3: LongSustain — RMS stays above floor for 2+ seconds after attack
//
// sustain=0.85 means the envelope holds at 85% of peak.  Measure 10 blocks
// starting at block 100 (~1.07s from note_on) — should still be well above zero.
// ---------------------------------------------------------------------------

TEST_F(GroupStringsPatchTest, LongSustain) {
    PRINT_TEST_HEADER(
        "Group Strings — Long Sustain (automated)",
        "ADSR sustain=0.85: signal should remain strong at t≈1.07s (well into sustain phase).",
        "engine_load_patch → note_on(C4) → skip 100 blocks → measure 10 blocks",
        "RMS > 0.002 at ~1.07s (sustain held)",
        sample_rate
    );

    ASSERT_EQ(engine_load_patch(engine(), kPatch), 0);
    engine_note_on(engine(), 60, 1.0f);  // C4

    const size_t FRAMES = 512;
    std::vector<float> buf(FRAMES * 2);

    for (int b = 0; b < 100; ++b)
        engine_process(engine(), buf.data(), FRAMES);

    double sum_sq = 0.0;
    for (int b = 0; b < 10; ++b) {
        engine_process(engine(), buf.data(), FRAMES);
        for (size_t i = 0; i < FRAMES; ++i) sum_sq += double(buf[i * 2]) * double(buf[i * 2]);
    }
    engine_note_off(engine(), 60);

    float rms = float(std::sqrt(sum_sq / double(FRAMES * 10)));
    std::cout << "[GroupStrings] Sustain RMS at t≈1.07s: " << rms << "\n";
    EXPECT_GT(rms, 0.002f) << "Expected sustained strings to remain above noise floor at 1s";
}

// ---------------------------------------------------------------------------
// Test 4: Audible — C major chord melody (G3 → E4 → C5)
// ---------------------------------------------------------------------------

TEST_F(GroupStringsPatchTest, ChordMelodyAudible) {
    PRINT_TEST_HEADER(
        "Group Strings — Chord Melody (audible)",
        "Slow bow-attack strings across G3/E4/C5 to hear the ensemble swell and warm Moog filter.",
        "engine_load_patch(group_strings.json) → engine_start → G3/B3/D4/G4 melody",
        "Audible slow-attack ensemble strings with warm low-res Moog filtering.",
        sample_rate
    );

    ASSERT_EQ(engine_load_patch(engine(), kPatch), 0);
    ASSERT_ENGINE_START(engine());

    constexpr int NOTE_MS    = 1200; // long gate — let attack fully develop
    constexpr int RELEASE_MS = 700;  // 0.5s release + margin

    const int notes[] = {55, 59, 62, 67};  // G3, B3, D4, G4
    std::cout << "[GroupStrings] Playing G3 → B3 → D4 → G4 (G major)…\n";
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
