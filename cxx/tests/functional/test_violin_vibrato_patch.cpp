/**
 * @file test_violin_vibrato_patch.cpp
 * @brief Functional tests for violin_vibrato.json.
 *
 * Patch topology:
 *   LFO1 (5.5 Hz, intensity=0.003) → VCO pitch_cv  (violin vibrato)
 *   VCO (saw=1.0, sub=0.2) → VCA ← ENV
 *   ENV: attack=0.3s, decay=0.0, sustain=1.0, release=0.4s
 *
 * Characteristic features:
 *   - 300ms attack bow swell (between bowed_bass at 250ms and juno_pad at 400ms)
 *   - Sustain=1.0 with no decay — full amplitude while held
 *   - 400ms release — notes hang after lifting the virtual bow
 *   - LFO at 5.5 Hz, ±0.003 V/oct (≈ ±3.6 cents) — realistic violin vibrato depth
 *
 * ADSR IIR timing at 48kHz (coeff = exp(-log9 / (T * sr))):
 *   attack=0.3s: level at ~50ms  ≈ 0.28;  level at ~420ms ≈ 0.96
 *   release=0.4s: after 300ms post-note-off, level ≈ 0.37 of sustain — still audible
 *
 * Tests:
 *   1. Smoke        — patch loads and note-on produces non-silent audio.
 *   2. AttackSwell  — RMS at ~420–500ms is ≥2.5× RMS at ~10–53ms, confirming
 *                     the 300ms bow attack is shaping VCA gain.
 *   3. ReleaseDecay — RMS at ~300ms after note-off remains > 0.001, confirming
 *                     the 400ms release tail.
 *   4. Audible      — short violin phrase on the four open strings (G3–D4–A4–E5),
 *                     each note held long enough to hear attack swell and vibrato.
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

class ViolinVibratoPatchTest : public ::testing::Test {
protected:
    void SetUp() override {
        test::init_test_environment();
        sample_rate = test::get_safe_sample_rate(0);
        engine_wrapper = std::make_unique<test::EngineWrapper>(sample_rate);
    }

    EngineHandle engine() { return engine_wrapper->get(); }

    int sample_rate;
    std::unique_ptr<test::EngineWrapper> engine_wrapper;

    static constexpr const char* kPatch = "patches/violin_vibrato.json";
};

// ---------------------------------------------------------------------------
// Test 1: Smoke — patch loads and produces audio
// ---------------------------------------------------------------------------

TEST_F(ViolinVibratoPatchTest, NoteOnProducesAudio) {
    PRINT_TEST_HEADER(
        "Violin — Smoke",
        "Note-on produces non-silent audio through VCO → VCA chain.",
        "engine_load_patch(violin_vibrato.json) → note_on → engine_process × 60",
        "RMS > 0.001 across 60 blocks (allowing the 300ms attack to build).",
        sample_rate
    );

    ASSERT_EQ(engine_load_patch(engine(), kPatch), 0);
    engine_note_on(engine(), 69, 1.0f);  // A4

    const size_t FRAMES = 512;
    std::vector<float> buf(FRAMES * 2);
    double sum_sq = 0.0;
    for (int b = 0; b < 60; ++b) {
        engine_process(engine(), buf.data(), FRAMES);
        for (size_t i = 0; i < FRAMES; ++i) sum_sq += double(buf[i * 2]) * double(buf[i * 2]);
    }
    float rms = float(std::sqrt(sum_sq / double(FRAMES * 60)));
    std::cout << "[Violin] 60-block RMS: " << rms << "\n";
    EXPECT_GT(rms, 0.001f) << "Expected non-silent output during note";

    engine_note_off(engine(), 69);
}

// ---------------------------------------------------------------------------
// Test 2: AttackSwell — RMS rises significantly over the 300ms attack
//
// ADSR IIR (attack=0.3s, sr=48kHz):
//   level at ~50ms  ≈ 1 - exp(-log9 * 50/300)  ≈ 0.28
//   level at ~420ms ≈ 1 - exp(-log9 * 420/300) ≈ 0.96
//
// EARLY window: blocks 1–5   (~10–53ms)   — envelope at ~21–28%
// LATE  window: blocks 39–46 (~416–491ms) — envelope at ~95–97%
//
// Expected ratio late/early ≥ 2.5.
// ---------------------------------------------------------------------------

TEST_F(ViolinVibratoPatchTest, AttackSwell) {
    PRINT_TEST_HEADER(
        "Violin — Attack Swell (automated)",
        "RMS at ~416–491ms is ≥2.5× RMS at ~10–53ms, confirming the 300ms "
        "bow attack is shaping VCA gain (analogous to bowing pressure building).",
        "engine_load_patch → note_on(A4) → engine_process → compare early vs late RMS",
        "rms_late / rms_early ≥ 2.5",
        sample_rate
    );

    ASSERT_EQ(engine_load_patch(engine(), kPatch), 0);
    engine_note_on(engine(), 69, 1.0f);  // A4

    const size_t FRAMES      = 512;
    const int    EARLY_START = 1;
    const int    EARLY_END   = 6;    // ~10–53ms
    const int    LATE_START  = 39;
    const int    LATE_END    = 47;   // ~416–501ms

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
    engine_note_off(engine(), 69);

    float rms_early = float(std::sqrt(early_sq / double(FRAMES * early_n)));
    float rms_late  = float(std::sqrt(late_sq  / double(FRAMES * late_n)));
    float ratio     = rms_early > 1e-6f ? rms_late / rms_early : 0.0f;

    std::cout << "[Violin] RMS early (~10–53ms):    " << rms_early << "\n";
    std::cout << "[Violin] RMS late  (~416–491ms):  " << rms_late  << "\n";
    std::cout << "[Violin] Ratio late/early:          " << ratio     << "\n";

    EXPECT_GT(rms_early, 1e-5f) << "No signal in early window — envelope may not be running";
    EXPECT_GT(ratio, 2.5f)
        << "Expected ≥2.5× RMS rise over 300ms attack (got " << ratio << ")";
}

// ---------------------------------------------------------------------------
// Test 3: ReleaseDecay — signal persists after note-off (400ms release)
//
// release=0.4s IIR: after 300ms, level ≈ sustain * exp(-log9 * 0.75) ≈ 0.37
//
// Sequence: hold A4 for 47 blocks (~501ms, at full sustain),
//           then note-off and measure RMS for the next 28 blocks (~299ms).
// ---------------------------------------------------------------------------

TEST_F(ViolinVibratoPatchTest, ReleaseDecay) {
    PRINT_TEST_HEADER(
        "Violin — Release Tail (automated)",
        "Signal persists ~300ms after note-off due to 400ms release envelope, "
        "mimicking the bow lifting off the string.",
        "note_on(A4) → 47 blocks → note_off → 28 blocks → assert RMS > 0.001",
        "RMS > 0.001 in the 28 blocks (~299ms) following note-off.",
        sample_rate
    );

    ASSERT_EQ(engine_load_patch(engine(), kPatch), 0);

    const size_t FRAMES         = 512;
    const int    HOLD_BLOCKS    = 47;   // ~501ms — past attack, at full sustain
    const int    RELEASE_BLOCKS = 28;   // ~299ms into 400ms release tail
    std::vector<float> buf(FRAMES * 2);

    engine_note_on(engine(), 69, 1.0f);
    for (int b = 0; b < HOLD_BLOCKS; ++b)
        engine_process(engine(), buf.data(), FRAMES);

    engine_note_off(engine(), 69);

    double sum_sq = 0.0;
    for (int b = 0; b < RELEASE_BLOCKS; ++b) {
        engine_process(engine(), buf.data(), FRAMES);
        for (size_t i = 0; i < FRAMES; ++i) sum_sq += double(buf[i * 2]) * double(buf[i * 2]);
    }

    float tail_rms = float(std::sqrt(sum_sq / double(FRAMES * RELEASE_BLOCKS)));
    std::cout << "[Violin] Release tail RMS (~299ms after note-off): " << tail_rms << "\n";
    EXPECT_GT(tail_rms, 0.001f)
        << "Expected signal for ~300ms after note-off (release=400ms)";
}

// ---------------------------------------------------------------------------
// Test 4: Audible — open-string violin phrase (G3–D4–A4–E5)
// ---------------------------------------------------------------------------

TEST_F(ViolinVibratoPatchTest, OpenStringPhraseAudible) {
    PRINT_TEST_HEADER(
        "Violin — Open String Phrase (audible)",
        "Play G3–D4–A4–E5 (the four violin open strings) to hear the 300ms bow "
        "attack swell, 5.5 Hz vibrato, and the 400ms release ring-out.",
        "engine_load_patch(violin_vibrato.json) → engine_start → G3/D4/A4/E5",
        "Audible violin-like tone with bow swell, vibrato, and release tail.",
        sample_rate
    );

    ASSERT_EQ(engine_load_patch(engine(), kPatch), 0);
    ASSERT_ENGINE_START(engine());

    constexpr int NOTE_MS    = 1400;  // long enough to hear full attack + vibrato
    constexpr int RELEASE_MS =  600;  // 400ms release + margin

    const int notes[] = {55, 62, 69, 76};  // G3, D4, A4, E5
    std::cout << "[Violin] Playing open strings G3 – D4 – A4 – E5…\n";
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
