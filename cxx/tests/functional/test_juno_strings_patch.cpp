/**
 * @file test_juno_strings_patch.cpp
 * @brief Functional tests for juno_strings.json — string pad with JUNO_CHORUS.
 *
 * Patch topology:
 *   LFO (0.35 Hz, intensity=0.002) → VCO pitch_cv  (ensemble shimmer)
 *   VCO (saw=1.0, sub=0.3, detune=8) → SH_FILTER (cutoff=1800, res=0.15)
 *   SH_FILTER → VCA ← ADSR (attack=0.35, decay=0.1, sustain=0.85, release=0.5)
 *   VCA → JUNO_CHORUS (mode=2, rate=0.5, depth=0.6)
 *
 * Characteristic features:
 *   - 350ms attack swell — slow string-pad onset
 *   - Detune=8 cents gives a subtle ensemble shimmer before the chorus
 *   - JUNO_CHORUS mode II creates measurable L/R stereo spread via opposing LFO phases
 *
 * Tests:
 *   1. NoteOnProducesAudio  — skip 40 blocks (slow attack), RMS > 0.001.
 *   2. StereoWidth          — chorus creates measurable L/R difference (spread > 0.05).
 *   3. SlowAttack           — blocks 2-6 RMS vs blocks 38-42 RMS: late > early * 1.5.
 *   4. ChordSequenceAudible — Am chord (A3/C4/E4/A4) live play.
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

class JunoStringsPatchTest : public ::testing::Test {
protected:
    void SetUp() override {
        test::init_test_environment();
        sample_rate = test::get_safe_sample_rate(0);
        engine_wrapper = std::make_unique<test::EngineWrapper>(sample_rate);
    }

    EngineHandle engine() { return engine_wrapper->get(); }

    int sample_rate;
    std::unique_ptr<test::EngineWrapper> engine_wrapper;

    static constexpr const char* kPatch = "patches/juno_strings.json";
    static constexpr size_t FRAMES = 512;
};

// ---------------------------------------------------------------------------
// Test 1: NoteOnProducesAudio — skip slow attack, then measure audio
// ---------------------------------------------------------------------------

TEST_F(JunoStringsPatchTest, NoteOnProducesAudio) {
    PRINT_TEST_HEADER(
        "Juno Strings — Smoke",
        "Note-on produces non-silent audio after the 350ms attack swell.",
        "engine_load_patch(juno_strings.json) → note_on → skip 40 blocks → measure 10 blocks",
        "RMS > 0.001 after the attack has built up.",
        sample_rate
    );

    ASSERT_EQ(engine_load_patch(engine(), kPatch), 0);
    engine_note_on(engine(), 48, 1.0f);  // C3

    std::vector<float> buf(FRAMES * 2);

    // Skip 40 blocks (~427ms) to let the 350ms attack build
    for (int b = 0; b < 40; ++b)
        engine_process(engine(), buf.data(), FRAMES);

    double sum_sq = 0.0;
    for (int b = 0; b < 10; ++b) {
        engine_process(engine(), buf.data(), FRAMES);
        for (size_t i = 0; i < FRAMES; ++i) sum_sq += double(buf[i * 2]) * double(buf[i * 2]);
    }
    float rms = float(std::sqrt(sum_sq / double(FRAMES * 10)));
    std::cout << "[JunoStrings] Post-attack RMS (10 blocks): " << rms << "\n";
    EXPECT_GT(rms, 0.001f) << "Expected non-silent output after attack builds";

    engine_note_off(engine(), 48);
}

// ---------------------------------------------------------------------------
// Test 2: StereoWidth — JUNO_CHORUS creates measurable L/R difference
//
// After the chorus LFOs diverge, L and R have opposing delay modulation.
// spread = sqrt(sum((L-R)^2) / sum(L^2)) should exceed 0.05 (5%).
// ---------------------------------------------------------------------------

TEST_F(JunoStringsPatchTest, StereoWidth) {
    PRINT_TEST_HEADER(
        "Juno Strings — Stereo Width",
        "JUNO_CHORUS (mode=2) creates measurable L/R difference from opposing LFO phases.",
        "note_on → skip 50 blocks → capture 10 blocks → compute |L-R| RMS / L RMS",
        "spread > 0.05 (5% stereo difference signal relative to signal level)",
        sample_rate
    );

    ASSERT_EQ(engine_load_patch(engine(), kPatch), 0);
    engine_note_on(engine(), 60, 1.0f);  // C4

    std::vector<float> buf(FRAMES * 2);

    // Skip 50 blocks (~533ms) for attack to complete and chorus LFOs to develop
    for (int b = 0; b < 50; ++b)
        engine_process(engine(), buf.data(), FRAMES);

    double diff_sq = 0.0, sig_sq = 0.0;
    for (int b = 0; b < 10; ++b) {
        engine_process(engine(), buf.data(), FRAMES);
        for (size_t i = 0; i < FRAMES; ++i) {
            float L = buf[i * 2], R = buf[i * 2 + 1];
            diff_sq += double(L - R) * double(L - R);
            sig_sq  += double(L) * double(L);
        }
    }

    engine_note_off(engine(), 60);

    float spread = float(std::sqrt(diff_sq / std::max(sig_sq, 1e-12)));
    std::cout << "[JunoStrings] Stereo spread (|L-R| RMS / L RMS): " << spread << "\n";
    EXPECT_GT(spread, 0.05f) << "Expected chorus to create measurable L/R difference";
}

// ---------------------------------------------------------------------------
// Test 3: SlowAttack — RMS grows from early to late blocks
//
// Early window: blocks 2-6   (~21-64ms)  — attack barely started
// Late window:  blocks 38-42 (~405-448ms) — approaching full sustain
// Expected: rms_late > rms_early * 1.5
// ---------------------------------------------------------------------------

TEST_F(JunoStringsPatchTest, SlowAttack) {
    PRINT_TEST_HEADER(
        "Juno Strings — Slow Attack Swell",
        "RMS at blocks 38-42 (~405-448ms) is >=1.5x the RMS at blocks 2-6 (~21-64ms), "
        "confirming the 350ms ADSR attack is shaping the VCA.",
        "note_on → capture early (blocks 2-6) and late (blocks 38-42) windows → compare",
        "rms_late > rms_early * 1.5",
        sample_rate
    );

    ASSERT_EQ(engine_load_patch(engine(), kPatch), 0);
    engine_note_on(engine(), 48, 1.0f);  // C3

    std::vector<float> buf(FRAMES * 2);
    double early_sq = 0.0; int early_n = 0;
    double late_sq  = 0.0; int late_n  = 0;

    const int EARLY_START = 2;
    const int EARLY_END   = 6;
    const int LATE_START  = 38;
    const int LATE_END    = 42;

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

    std::cout << "[JunoStrings] RMS early (blocks 2-6):   " << rms_early << "\n";
    std::cout << "[JunoStrings] RMS late  (blocks 38-42): " << rms_late  << "\n";
    std::cout << "[JunoStrings] Ratio late/early:          " << ratio     << "\n";

    EXPECT_GT(rms_late, rms_early * 1.5f)
        << "Expected RMS to grow by at least 1.5x during 350ms attack "
        << "(early=" << rms_early << " late=" << rms_late << ")";
}

// ---------------------------------------------------------------------------
// Test 4: ChordSequenceAudible — Am chord with slow pad swell
// ---------------------------------------------------------------------------

TEST_F(JunoStringsPatchTest, ChordSequenceAudible) {
    PRINT_TEST_HEADER(
        "Juno Strings — Am Chord (audible)",
        "Play Am chord (A3/C4/E4/A4) with slow 350ms attack swell and JUNO_CHORUS stereo width. "
        "Notes held together, then released with 500ms tail.",
        "engine_load_patch(juno_strings.json) → engine_start → A3/C4/E4/A4 → release",
        "Audible lush pad swell with stereo chorus width.",
        sample_rate
    );

    ASSERT_EQ(engine_load_patch(engine(), kPatch), 0);
    ASSERT_ENGINE_START(engine());

    constexpr int NOTE_MS    = 1200;
    constexpr int RELEASE_MS =  600;

    const int notes[] = {57, 60, 64, 69};  // A3, C4, E4, A4
    std::cout << "[JunoStrings] Playing Am chord (A3/C4/E4/A4)...\n";
    for (int n : notes) engine_note_on(engine(), n, 0.8f);

    std::this_thread::sleep_for(std::chrono::milliseconds(NOTE_MS));
    for (int n : notes) engine_note_off(engine(), n);

    // Let the 500ms release ring out
    std::this_thread::sleep_for(std::chrono::milliseconds(RELEASE_MS));
    engine_stop(engine());
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
