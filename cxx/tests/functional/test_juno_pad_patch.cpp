/**
 * @file test_juno_pad_patch.cpp
 * @brief Functional tests for juno_pad.json.
 *
 * Patch topology:
 *   LFO1 (0.5 Hz, intensity=0.004) → VCO pitch_cv  (ultra-slow vibrato)
 *   VCO (saw=1.0, sine=0.3) → VCA ← ENV
 *   ENV: attack=0.4s, decay=0.1s, sustain=0.8, release=0.6s
 *
 * Characteristic features:
 *   - 400ms attack swell — the hallmark of a lush pad
 *   - 600ms release tail — notes hang in the air after key-up
 *   - LFO at 0.5 Hz (one cycle per 2s) gives a very gentle, slow pitch drift
 *
 * ADSR IIR timing at 48kHz (coeff = exp(-log9 / (T * sr))):
 *   attack=0.4s: level at ~50ms  ≈ 0.22;  level at ~500ms ≈ 0.92
 *   release=0.6s: at 300ms after note-off, sustain decays to ~27% of sustain level
 *
 * Tests:
 *   1. Smoke       — patch loads and note-on produces non-silent audio.
 *   2. SlowAttack  — RMS at ~500ms is ≥3× RMS at ~10–53ms, confirming 400ms swell.
 *   3. LongRelease — RMS 300ms after note-off exceeds 0.001, confirming 600ms tail.
 *   4. Audible     — C3–E3–G3–C4 slow arpeggiated pad chord, notes overlap via release.
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

class JunoPadPatchTest : public ::testing::Test {
protected:
    void SetUp() override {
        test::init_test_environment();
        sample_rate = test::get_safe_sample_rate(0);
        engine_wrapper = std::make_unique<test::EngineWrapper>(sample_rate);
    }

    EngineHandle engine() { return engine_wrapper->get(); }

    int sample_rate;
    std::unique_ptr<test::EngineWrapper> engine_wrapper;

    static constexpr const char* kPatch = "patches/juno_pad.json";
};

// ---------------------------------------------------------------------------
// Test 1: Smoke — patch loads and produces audio
// ---------------------------------------------------------------------------

TEST_F(JunoPadPatchTest, NoteOnProducesAudio) {
    PRINT_TEST_HEADER(
        "Juno Pad — Smoke",
        "Note-on produces non-silent audio through VCO → VCA chain.",
        "engine_load_patch(juno_pad.json) → note_on → engine_process × 60",
        "RMS > 0.001 across 60 blocks (allowing the 400ms attack to build).",
        sample_rate
    );

    ASSERT_EQ(engine_load_patch(engine(), kPatch), 0);
    engine_note_on(engine(), 48, 1.0f);  // C3

    const size_t FRAMES = 512;
    std::vector<float> buf(FRAMES * 2);
    double sum_sq = 0.0;
    for (int b = 0; b < 60; ++b) {
        engine_process(engine(), buf.data(), FRAMES);
        for (size_t i = 0; i < FRAMES; ++i) sum_sq += double(buf[i * 2]) * double(buf[i * 2]);
    }
    float rms = float(std::sqrt(sum_sq / double(FRAMES * 60)));
    std::cout << "[JunoPad] 60-block RMS: " << rms << "\n";
    EXPECT_GT(rms, 0.001f) << "Expected non-silent output during note";

    engine_note_off(engine(), 48);
}

// ---------------------------------------------------------------------------
// Test 2: SlowAttack — RMS swells dramatically over the 400ms attack
//
// EARLY window: blocks 1–5   (~10–53ms)   — envelope level ~17–22%
// LATE  window: blocks 46–54 (~491–576ms) — envelope level ~90–94%
//
// Expected ratio late/early ≥ 3.0.
// ---------------------------------------------------------------------------

TEST_F(JunoPadPatchTest, SlowAttackRmsRises) {
    PRINT_TEST_HEADER(
        "Juno Pad — Slow Attack Swell (automated)",
        "RMS at ~491–576ms is ≥3× the RMS at ~10–53ms, confirming the 400ms "
        "ADSR attack swell is shaping VCA gain.",
        "engine_load_patch → note_on(C3) → engine_process → compare early vs late RMS",
        "rms_late / rms_early ≥ 3.0",
        sample_rate
    );

    ASSERT_EQ(engine_load_patch(engine(), kPatch), 0);
    engine_note_on(engine(), 48, 1.0f);  // C3

    const size_t FRAMES       = 512;
    const int    EARLY_START  = 1;
    const int    EARLY_END    = 6;    // ~10–53ms
    const int    LATE_START   = 46;
    const int    LATE_END     = 55;   // ~491–587ms

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
    engine_note_off(engine(), 48);

    float rms_early = float(std::sqrt(early_sq / double(FRAMES * early_n)));
    float rms_late  = float(std::sqrt(late_sq  / double(FRAMES * late_n)));
    float ratio     = rms_early > 1e-6f ? rms_late / rms_early : 0.0f;

    std::cout << "[JunoPad] RMS early (~10–53ms):    " << rms_early << "\n";
    std::cout << "[JunoPad] RMS late  (~491–576ms):  " << rms_late  << "\n";
    std::cout << "[JunoPad] Ratio late/early:         " << ratio     << "\n";

    EXPECT_GT(rms_early, 1e-5f) << "No signal in early window — envelope may not be running";
    EXPECT_GT(ratio, 3.0f)
        << "Expected ≥3× RMS rise over 400ms attack (got " << ratio << ")";
}

// ---------------------------------------------------------------------------
// Test 3: LongRelease — signal persists well after note-off
//
// Sustain level ≈ 0.80. Release=0.6s IIR:
//   coeff = exp(-log9 / (0.6 * 48000))
//   After 300ms: level ≈ 0.8 * exp(-log9 * 0.5) ≈ 0.8 * 0.333 ≈ 0.267
//
// Sequence: hold C3 for 55 blocks (~587ms, near full sustain),
//           then note-off and measure RMS for the next 28 blocks (~299ms).
// ---------------------------------------------------------------------------

TEST_F(JunoPadPatchTest, LongReleasePersists) {
    PRINT_TEST_HEADER(
        "Juno Pad — Long Release Tail (automated)",
        "Signal persists for ~300ms after note-off due to 600ms release envelope.",
        "note_on(C3) → 55 blocks → note_off → 28 blocks → assert RMS > 0.001",
        "RMS > 0.001 in the 28 blocks (~299ms) following note-off.",
        sample_rate
    );

    ASSERT_EQ(engine_load_patch(engine(), kPatch), 0);

    const size_t FRAMES         = 512;
    const int    HOLD_BLOCKS    = 55;   // ~587ms — past attack+decay, stable sustain
    const int    RELEASE_BLOCKS = 28;   // ~299ms — should still be ~27% of sustain
    std::vector<float> buf(FRAMES * 2);

    engine_note_on(engine(), 48, 1.0f);
    for (int b = 0; b < HOLD_BLOCKS; ++b)
        engine_process(engine(), buf.data(), FRAMES);

    engine_note_off(engine(), 48);

    double sum_sq = 0.0;
    for (int b = 0; b < RELEASE_BLOCKS; ++b) {
        engine_process(engine(), buf.data(), FRAMES);
        for (size_t i = 0; i < FRAMES; ++i) sum_sq += double(buf[i * 2]) * double(buf[i * 2]);
    }

    float tail_rms = float(std::sqrt(sum_sq / double(FRAMES * RELEASE_BLOCKS)));
    std::cout << "[JunoPad] Release tail RMS (~299ms after note-off): " << tail_rms << "\n";
    EXPECT_GT(tail_rms, 0.001f)
        << "Expected signal for ~300ms after note-off (release=600ms)";
}

// ---------------------------------------------------------------------------
// Test 4: Audible — slow arpeggiated pad chord
// ---------------------------------------------------------------------------

TEST_F(JunoPadPatchTest, PadChordAudible) {
    PRINT_TEST_HEADER(
        "Juno Pad — Arpeggiated Chord (audible)",
        "Play C3–E3–G3–C4 slowly so the 600ms release overlaps successive notes, "
        "creating a lush pad wash. Demonstrates 400ms attack swell and slow vibrato.",
        "engine_load_patch(juno_pad.json) → engine_start → C3/E3/G3/C4 staggered",
        "Audible pad swell with overlapping release tails and gentle LFO drift.",
        sample_rate
    );

    ASSERT_EQ(engine_load_patch(engine(), kPatch), 0);
    ASSERT_EQ(engine_start(engine()), 0);

    // Notes overlap: each held 900ms with 300ms stagger → release blurs together
    constexpr int HOLD_MS   = 1200;
    constexpr int STAGGER_MS =  350;

    const int notes[] = {48, 52, 55, 60};  // C3, E3, G3, C4
    std::cout << "[JunoPad] Playing C3–E3–G3–C4 pad chord (staggered)…\n";
    for (int i = 0; i < 4; ++i) {
        engine_note_on(engine(), notes[i], 0.85f);
        if (i < 3)
            std::this_thread::sleep_for(std::chrono::milliseconds(STAGGER_MS));
    }
    // Hold the full chord
    std::this_thread::sleep_for(std::chrono::milliseconds(HOLD_MS));
    for (int midi : notes) engine_note_off(engine(), midi);

    // Let the 600ms release ring out
    std::this_thread::sleep_for(std::chrono::milliseconds(1200));
    engine_stop(engine());
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
