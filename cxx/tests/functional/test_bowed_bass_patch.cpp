/**
 * @file test_bowed_bass_patch.cpp
 * @brief Functional tests for bowed_bass.json.
 *
 * Patch topology:
 *   VCO (saw+tri+sub) → VCA ← ENV (attack=0.25s, decay=0, sustain=1.0, release=0.5s)
 *
 * The characteristic of this patch is the slow bow attack: the ADSR IIR rises
 * gradually over ~350ms before reaching near-sustain level. This mimics a cello
 * or double-bass bow stroke where pressure builds on the string.
 *
 * Tests:
 *   1. Smoke       — patch loads and note-on produces non-silent audio.
 *   2. SlowAttack  — RMS during the late attack window (≥350ms) is significantly
 *                    higher than RMS during the early attack window (~85ms),
 *                    confirming the ADSR envelope is shaping amplitude correctly.
 *   3. Audible     — a few sustained bowed bass notes (C2, G2, C3).
 */

#include <gtest/gtest.h>
#include "../TestHelper.hpp"
#include <vector>
#include <cmath>
#include <thread>
#include <chrono>

// ---------------------------------------------------------------------------
// Fixture
// ---------------------------------------------------------------------------

class BowedBassPatchTest : public ::testing::Test {
protected:
    void SetUp() override {
        test::init_test_environment();
        sample_rate = test::get_safe_sample_rate(0);
        engine_wrapper = std::make_unique<test::EngineWrapper>(sample_rate);
    }

    EngineHandle engine() { return engine_wrapper->get(); }

    int sample_rate;
    std::unique_ptr<test::EngineWrapper> engine_wrapper;

    static constexpr const char* kPatch = "patches/bowed_bass.json";
};

// ---------------------------------------------------------------------------
// Test 1: Smoke — patch loads and produces audio
// ---------------------------------------------------------------------------

TEST_F(BowedBassPatchTest, NoteOnProducesAudio) {
    PRINT_TEST_HEADER(
        "Bowed Bass — Smoke",
        "Note-on produces non-silent audio through VCO → VCA chain.",
        "engine_load_patch(bowed_bass.json) → note_on → engine_process × 60",
        "RMS > 0.001 across 60 blocks (giving the 0.25s attack time to build).",
        sample_rate
    );

    ASSERT_EQ(engine_load_patch(engine(), kPatch), 0);
    engine_note_on(engine(), 36, 1.0f);  // C2

    const size_t FRAMES = 512;
    std::vector<float> buf(FRAMES * 2);
    double sum_sq = 0.0;
    for (int b = 0; b < 60; ++b) {
        engine_process(engine(), buf.data(), FRAMES);
        for (size_t i = 0; i < FRAMES; ++i) sum_sq += double(buf[i * 2]) * double(buf[i * 2]);
    }
    float rms = float(std::sqrt(sum_sq / double(FRAMES * 60)));
    std::cout << "[BowedBass] 60-block RMS: " << rms << "\n";
    EXPECT_GT(rms, 0.001f) << "Expected non-silent output during note";

    engine_note_off(engine(), 36);
}

// ---------------------------------------------------------------------------
// Test 2: SlowAttack — RMS rises significantly over the 0.25s attack time
//
// ADSR IIR: coeff = exp(-log(9) / (T * sr))
//   attack=0.25s, sr=48000: level at 85ms  ≈ 1 - exp(-0.880) ≈ 0.585
//                           level at 350ms ≈ 1 - exp(-3.079) ≈ 0.954
//
// EARLY window: blocks 5–12  (~53–128ms  into note)
// LATE  window: blocks 33–40 (~352–426ms into note, near full sustain)
//
// Since VCA output is proportional to envelope level, RMS_late / RMS_early
// should be ≥ 1.3 (conservatively accounting for IIR non-linearity).
// ---------------------------------------------------------------------------

TEST_F(BowedBassPatchTest, SlowAttackRmsRises) {
    PRINT_TEST_HEADER(
        "Bowed Bass — Slow Attack (automated)",
        "RMS rises significantly from early attack (~85ms) to late attack (~375ms), "
        "confirming the 0.25s ADSR envelope is shaping VCA gain correctly.",
        "engine_load_patch → note_on(C2) → engine_process → compare early vs late RMS",
        "rms_late / rms_early ≥ 1.3",
        sample_rate
    );

    ASSERT_EQ(engine_load_patch(engine(), kPatch), 0);
    engine_note_on(engine(), 36, 1.0f);  // C2

    const size_t FRAMES        = 512;
    const int    EARLY_START   = 5;    // skip ~53ms of startup noise
    const int    EARLY_END     = 13;   // window ends at ~139ms
    const int    LATE_START    = 33;   // ~352ms — near plateau
    const int    LATE_END      = 41;   // ~437ms

    const int TOTAL = LATE_END;

    std::vector<float> buf(FRAMES * 2);
    double early_sq = 0.0;  int early_n = 0;
    double late_sq  = 0.0;  int late_n  = 0;

    for (int b = 0; b < TOTAL; ++b) {
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

    std::cout << "[BowedBass] RMS early (~85ms):  " << rms_early << "\n";
    std::cout << "[BowedBass] RMS late  (~375ms): " << rms_late  << "\n";
    std::cout << "[BowedBass] Ratio late/early:   " << ratio     << "\n";

    EXPECT_GT(rms_early, 0.001f) << "No signal during early window — envelope may not be running";
    EXPECT_GT(ratio, 1.3f)
        << "Expected RMS to rise by ≥1.3× over the 0.25s attack (got " << ratio << ")";
}

// ---------------------------------------------------------------------------
// Test 3: Audible — sustained bowed bass notes
// ---------------------------------------------------------------------------

TEST_F(BowedBassPatchTest, SustainedNotesAudible) {
    PRINT_TEST_HEADER(
        "Bowed Bass — Sustained Notes (audible)",
        "Play C2, G2, C3 with long bows to hear the slow attack and warm sustain.",
        "engine_load_patch(bowed_bass.json) → engine_start → C2/G2/C3 × 2s each",
        "Audible slow bow attack with warm saw+triangle sustain on bass notes.",
        sample_rate
    );

    ASSERT_EQ(engine_load_patch(engine(), kPatch), 0);
    ASSERT_ENGINE_START(engine());

    constexpr int NOTE_MS    = 1800;  // long bow
    constexpr int RELEASE_MS =  700;  // 0.5s release + margin

    const int notes[] = {36, 43, 48};  // C2, G2, C3
    std::cout << "[BowedBass] Playing C2 – G2 – C3 with slow bowing…\n";
    for (int midi : notes) {
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
