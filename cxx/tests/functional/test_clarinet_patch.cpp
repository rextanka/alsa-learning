/**
 * @file test_clarinet_patch.cpp
 * @brief Functional tests for clarinet.json — hollow pulse-wave timbre.
 *
 * Patch topology (Practical Synthesis Vol. 2, Ch. 1 clarinet):
 *   COMPOSITE_GENERATOR (pulse, 50% width) → SH_FILTER (LP, cutoff=2200 Hz, res=0.3)
 *       → VCA ← ADSR_ENVELOPE (attack=12ms, decay=0, sustain=1.0, release=60ms)
 *
 * The clarinet character comes from the pulse (square) wave: at 50% duty cycle
 * even harmonics cancel, leaving only odd partials (1st, 3rd, 5th...) — the
 * hollow, woody timbre that distinguishes clarinet from oboe (saw-based).
 * The LP filter at 2200 Hz with keyboard tracking softens the high odd harmonics
 * while preserving the characteristic nasal quality.
 *
 * Key assertions:
 *   1. Smoke       — note_on produces non-silent audio after attack (>12ms).
 *   2. FastAttack  — attack=12ms: RMS at blocks 2–4 is already near the
 *                    sustained level reached at blocks 10–14.
 *   3. LongSustain — sustain=1.0: signal stays strong for 1+ seconds.
 *   4. Audible     — ascending C major scale (C4/E4/G4/A4/C5).
 *
 * Timing at 48 kHz, block=512 (~10.67ms/block):
 *   attack=12ms ≈ 1.1 blocks — essentially instant.
 *   Blocks 2–4:   ~21–43ms   (attack complete, full sustain level)
 *   Blocks 10–14: ~107–149ms (sustained, same level expected)
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

class ClarinetPatchTest : public ::testing::Test {
protected:
    void SetUp() override {
        test::init_test_environment();
        sample_rate = test::get_safe_sample_rate(0);
        engine_wrapper = std::make_unique<test::EngineWrapper>(sample_rate);
    }

    EngineHandle engine() { return engine_wrapper->get(); }

    int sample_rate;
    std::unique_ptr<test::EngineWrapper> engine_wrapper;

    static constexpr const char* kPatch = "patches/clarinet.json";
};

// ---------------------------------------------------------------------------
// Test 1: Smoke — non-silent audio after attack completes (~12ms)
// ---------------------------------------------------------------------------

TEST_F(ClarinetPatchTest, NoteOnProducesAudio) {
    PRINT_TEST_HEADER(
        "Clarinet — Smoke",
        "Pulse-wave VCO through LP filter produces non-silent audio after 12ms attack.",
        "engine_load_patch(clarinet.json) → note_on(A4) → skip 3 blocks → measure 5",
        "RMS > 0.001 after attack completes.",
        sample_rate
    );

    ASSERT_EQ(engine_load_patch(engine(), kPatch), 0);
    engine_note_on(engine(), 69, 1.0f);  // A4

    const size_t FRAMES = 512;
    std::vector<float> buf(FRAMES * 2);

    for (int b = 0; b < 3; ++b)
        engine_process(engine(), buf.data(), FRAMES);

    double sum_sq = 0.0;
    for (int b = 0; b < 5; ++b) {
        engine_process(engine(), buf.data(), FRAMES);
        for (size_t i = 0; i < FRAMES; ++i) sum_sq += double(buf[i * 2]) * double(buf[i * 2]);
    }
    engine_note_off(engine(), 69);

    float rms = float(std::sqrt(sum_sq / double(FRAMES * 5)));
    std::cout << "[Clarinet] Post-attack RMS: " << rms << "\n";
    EXPECT_GT(rms, 0.001f) << "Expected non-silent audio from pulse+LP chain";
}

// ---------------------------------------------------------------------------
// Test 2: FastAttack — attack=12ms means sustain level reached by block 2
//
// IIR attack reaches 99% in ~3 time constants ≈ 36ms ≈ 3.4 blocks.
// At block 2 (~21ms), envelope ≈ 1 − e^(−log99 × 0.021/0.012) ≈ 97% of plateau.
// Expected: rms_early / rms_late > 0.6 (nearly at plateau by block 2)
// ---------------------------------------------------------------------------

TEST_F(ClarinetPatchTest, FastAttack) {
    PRINT_TEST_HEADER(
        "Clarinet — Fast Attack (automated)",
        "ADSR attack=12ms: RMS at blocks 2–4 (~21–43ms) is close to blocks 10–14 (~107ms).",
        "engine_load_patch → note_on(A4) → 14 blocks → compare early vs late window RMS",
        "rms_early / rms_late > 0.6 (attack almost complete by block 2)",
        sample_rate
    );

    ASSERT_EQ(engine_load_patch(engine(), kPatch), 0);
    engine_note_on(engine(), 69, 1.0f);  // A4

    const size_t FRAMES      = 512;
    const int    EARLY_START = 2;
    const int    EARLY_END   = 5;   // ~21–53ms (attack nearly complete)
    const int    LATE_START  = 10;
    const int    LATE_END    = 14;  // ~107–149ms (full sustain)

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
    float ratio     = rms_late > 1e-9f ? rms_early / rms_late : 0.0f;

    std::cout << "[Clarinet] RMS early (~21–53ms):   " << rms_early << "\n";
    std::cout << "[Clarinet] RMS late  (~107–149ms): " << rms_late  << "\n";
    std::cout << "[Clarinet] Ratio early/late:        " << ratio     << "\n";

    EXPECT_GT(rms_late, 0.001f) << "No sustained signal at late window";
    EXPECT_GT(ratio, 0.6f)
        << "Expected early RMS to be >60% of late (fast 12ms attack); ratio=" << ratio;
}

// ---------------------------------------------------------------------------
// Test 3: LongSustain — sustain=1.0 keeps signal alive well past 1 second
// ---------------------------------------------------------------------------

TEST_F(ClarinetPatchTest, LongSustain) {
    PRINT_TEST_HEADER(
        "Clarinet — Long Sustain (automated)",
        "ADSR sustain=1.0: signal should remain strong at t≈1.07s.",
        "engine_load_patch → note_on(A4) → skip 100 blocks → measure 10 blocks",
        "RMS > 0.001 at ~1.07s (gate held)",
        sample_rate
    );

    ASSERT_EQ(engine_load_patch(engine(), kPatch), 0);
    engine_note_on(engine(), 69, 1.0f);  // A4

    const size_t FRAMES = 512;
    std::vector<float> buf(FRAMES * 2);

    for (int b = 0; b < 100; ++b)
        engine_process(engine(), buf.data(), FRAMES);

    double sum_sq = 0.0;
    for (int b = 0; b < 10; ++b) {
        engine_process(engine(), buf.data(), FRAMES);
        for (size_t i = 0; i < FRAMES; ++i) sum_sq += double(buf[i * 2]) * double(buf[i * 2]);
    }
    engine_note_off(engine(), 69);

    float rms = float(std::sqrt(sum_sq / double(FRAMES * 10)));
    std::cout << "[Clarinet] Sustain RMS at t≈1.07s: " << rms << "\n";
    EXPECT_GT(rms, 0.001f) << "Expected sustained clarinet tone to remain above noise floor at 1s";
}

// ---------------------------------------------------------------------------
// Test 4: Audible — ascending C major scale (C4/E4/G4/A4/C5)
// ---------------------------------------------------------------------------

TEST_F(ClarinetPatchTest, CMajorScaleAudible) {
    PRINT_TEST_HEADER(
        "Clarinet — C Major Scale (audible)",
        "Pulse-wave clarinet timbre across C4/E4/G4/A4/C5 — hollow, woody tone "
        "with near-instant attack, sustain, and short release.",
        "engine_load_patch(clarinet.json) → engine_start → C4/E4/G4/A4/C5",
        "Audible hollow reed-like timbre with instant-onset woodwind character.",
        sample_rate
    );

    ASSERT_EQ(engine_load_patch(engine(), kPatch), 0);
    ASSERT_ENGINE_START(engine());

    constexpr int NOTE_MS    = 500;
    constexpr int RELEASE_MS = 200;  // 60ms release + margin

    const int notes[] = {60, 64, 67, 69, 72};  // C4, E4, G4, A4, C5
    std::cout << "[Clarinet] Playing C4 → E4 → G4 → A4 → C5…\n";
    for (int midi : notes) {
        engine_note_on(engine(), midi, 0.85f);
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
