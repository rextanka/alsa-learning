/**
 * @file test_brass_patch.cpp
 * @brief Functional tests for brass.json (Brass / Horn patch).
 *
 * Patch topology:
 *   VCO (saw + pulse) → VCA ← ENV (attack=10ms, decay=80ms, sustain=0.65, release=150ms)
 *
 * The defining character of this patch is the brass "blat": a fast attack to peak
 * amplitude, followed by a decay to a 65% sustain level that persists while the key
 * is held. The peak-to-sustain drop is approximately 1/0.65 ≈ 1.54×.
 *
 * ADSR IIR timing at 48kHz:
 *   Attack peak reached ~20–30ms after note-on (coeff = exp(-log9/(0.01*sr))).
 *   Decay settles to sustain by ~300ms (coeff = exp(-log9/(0.08*sr))).
 *
 * Tests:
 *   1. Smoke         — patch loads and note-on produces non-silent audio.
 *   2. DecayToSustain — RMS near peak (21–53ms) is ≥1.3× the settled sustain
 *                      RMS (~320–406ms), confirming the ADSR decay stage works.
 *   3. Audible       — C4–E4–G4–C5 ascending arpeggio (brass fanfare phrase).
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

class BrassPatchTest : public ::testing::Test {
protected:
    void SetUp() override {
        test::init_test_environment();
        sample_rate = test::get_safe_sample_rate(0);
        engine_wrapper = std::make_unique<test::EngineWrapper>(sample_rate);
    }

    EngineHandle engine() { return engine_wrapper->get(); }

    int sample_rate;
    std::unique_ptr<test::EngineWrapper> engine_wrapper;

    static constexpr const char* kPatch = "patches/brass.json";
};

// ---------------------------------------------------------------------------
// Test 1: Smoke — patch loads and produces audio
// ---------------------------------------------------------------------------

TEST_F(BrassPatchTest, NoteOnProducesAudio) {
    PRINT_TEST_HEADER(
        "Brass — Smoke",
        "Note-on produces non-silent audio through VCO → VCA chain.",
        "engine_load_patch(brass.json) → note_on → engine_process × 10",
        "RMS > 0.001 across 10 blocks.",
        sample_rate
    );

    ASSERT_EQ(engine_load_patch(engine(), kPatch), 0);
    engine_note_on(engine(), 60, 1.0f);  // C4

    const size_t FRAMES = 512;
    std::vector<float> buf(FRAMES * 2);
    double sum_sq = 0.0;
    for (int b = 0; b < 10; ++b) {
        engine_process(engine(), buf.data(), FRAMES);
        for (size_t i = 0; i < FRAMES; ++i) sum_sq += double(buf[i * 2]) * double(buf[i * 2]);
    }
    float rms = float(std::sqrt(sum_sq / double(FRAMES * 10)));
    std::cout << "[Brass] 10-block RMS: " << rms << "\n";
    EXPECT_GT(rms, 0.001f) << "Expected non-silent output at onset";

    engine_note_off(engine(), 60);
}

// ---------------------------------------------------------------------------
// Test 2: DecayToSustain — initial peak louder than settled sustain
//
// ADSR: attack=10ms → peak near blocks 2–5 (~21–53ms from note-on).
// Decay: 80ms time constant → settled at sustain=0.65 by ~300ms.
// SUSTAIN window: blocks 30–38 (~320–406ms).
//
// RMS ∝ amplitude, and sustain=0.65 means sustain ≈ 65% of peak.
// Expected ratio: rms_peak / rms_sustain ≥ 1.3.
// ---------------------------------------------------------------------------

TEST_F(BrassPatchTest, DecayToSustain) {
    PRINT_TEST_HEADER(
        "Brass — Decay to Sustain (automated)",
        "RMS near peak (21–53ms) is ≥1.3× the settled sustain RMS (~320–406ms), "
        "confirming the ADSR decay stage drops from 1.0 to sustain=0.65.",
        "engine_load_patch → note_on(C4) → engine_process → compare peak vs sustain RMS",
        "rms_peak / rms_sustain ≥ 1.3",
        sample_rate
    );

    ASSERT_EQ(engine_load_patch(engine(), kPatch), 0);
    engine_note_on(engine(), 60, 1.0f);  // C4

    const size_t FRAMES        = 512;
    const int    PEAK_START    = 2;    // ~21ms — past 10ms attack, near peak
    const int    PEAK_END      = 6;    // ~64ms
    const int    SUSTAIN_START = 30;   // ~320ms — decay fully settled
    const int    SUSTAIN_END   = 38;   // ~406ms

    std::vector<float> buf(FRAMES * 2);
    double peak_sq = 0.0;    int peak_n    = 0;
    double sustain_sq = 0.0; int sustain_n = 0;

    for (int b = 0; b < SUSTAIN_END; ++b) {
        engine_process(engine(), buf.data(), FRAMES);
        double block_sq = 0.0;
        for (size_t i = 0; i < FRAMES; ++i) block_sq += double(buf[i * 2]) * double(buf[i * 2]);
        if (b >= PEAK_START    && b < PEAK_END)    { peak_sq    += block_sq; ++peak_n;    }
        if (b >= SUSTAIN_START && b < SUSTAIN_END) { sustain_sq += block_sq; ++sustain_n; }
    }
    engine_note_off(engine(), 60);

    float rms_peak    = float(std::sqrt(peak_sq    / double(FRAMES * peak_n)));
    float rms_sustain = float(std::sqrt(sustain_sq / double(FRAMES * sustain_n)));
    float ratio       = rms_sustain > 1e-6f ? rms_peak / rms_sustain : 0.0f;

    std::cout << "[Brass] RMS peak    (~21–53ms): " << rms_peak    << "\n";
    std::cout << "[Brass] RMS sustain (~320ms+):  " << rms_sustain << "\n";
    std::cout << "[Brass] Ratio peak/sustain:     " << ratio       << "\n";

    EXPECT_GT(rms_sustain, 0.001f) << "No signal during sustain window — envelope may have expired";
    EXPECT_GT(ratio, 1.3f)
        << "Expected peak/sustain ratio ≥ 1.3 (sustain=0.65 → ratio≈1.54); got " << ratio;
}

// ---------------------------------------------------------------------------
// Test 3: Audible — ascending brass fanfare
// ---------------------------------------------------------------------------

TEST_F(BrassPatchTest, FanfareAudible) {
    PRINT_TEST_HEADER(
        "Brass — Fanfare (audible)",
        "Play C4–E4–G4–C5 as a rising brass arpeggio to hear the attack blat, "
        "sustain character, and release.",
        "engine_load_patch(brass.json) → engine_start → C4 / E4 / G4 / C5",
        "Audible brass fanfare with characteristic blat and 65% sustain.",
        sample_rate
    );

    ASSERT_EQ(engine_load_patch(engine(), kPatch), 0);
    ASSERT_EQ(engine_start(engine()), 0);

    constexpr int NOTE_MS    = 400;   // held duration
    constexpr int RELEASE_MS = 300;   // 150ms release + margin

    const int notes[] = {60, 64, 67, 72};  // C4, E4, G4, C5
    std::cout << "[Brass] Playing C4 – E4 – G4 – C5 fanfare…\n";
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
