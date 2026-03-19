/**
 * @file test_rain_patch.cpp
 * @brief Functional tests for rain.json — atmospheric bandpass noise texture.
 *
 * Patch topology (Practical Synthesis Vol. 2, Fig 3-25 rain approximation):
 *   WHITE_NOISE → MOOG_FILTER (LP, cutoff=5000 Hz, res=0.25)
 *              → HIGH_PASS_FILTER (cutoff=200 Hz, res=0.1)
 *              → VCA ← ADSR_ENVELOPE (attack=0.5s, decay=0.1s,
 *                                     sustain=1.0, release=2.0s)
 *
 * The LP+HPF cascade creates a broadband but band-limited noise texture —
 * the upper LP at 5kHz removes harsh hiss while the HPF at 200 Hz removes
 * subsonic rumble, leaving the characteristic mid/upper-mid noise of rain.
 * The slow attack (0.5s) and long release (2.0s) produce a realistic
 * fade-in and fade-out of the rain effect.
 *
 * Key assertions:
 *   1. Smoke      — note_on produces non-silent audio.
 *   2. SlowAttack — RMS grows from early (blocks 5–10) to late (blocks 50–60):
 *                   attack=0.5s ≈ 46.9 blocks before plateau.
 *   3. LongSustain — signal remains well above zero at block 100 (t≈1.07s).
 *   4. Audible    — a 6-second rain wash to demonstrate the full effect.
 *
 * Timing at 48 kHz, block=512 (~10.67ms/block):
 *   attack=0.5s ≈ 46.9 blocks.
 *   At block 7 (~75ms): envelope ≈ 1−e^(−log99 × 0.075/0.5) ≈ 33% of plateau.
 *   At block 55 (~587ms): envelope ≈ plateau.
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

class RainPatchTest : public ::testing::Test {
protected:
    void SetUp() override {
        test::init_test_environment();
        sample_rate = test::get_safe_sample_rate(0);
        engine_wrapper = std::make_unique<test::EngineWrapper>(sample_rate);
    }

    EngineHandle engine() { return engine_wrapper->get(); }

    int sample_rate;
    std::unique_ptr<test::EngineWrapper> engine_wrapper;

    static constexpr const char* kPatch = "patches/rain.json";
};

// ---------------------------------------------------------------------------
// Test 1: Smoke — note-on eventually produces non-silent audio
// ---------------------------------------------------------------------------

TEST_F(RainPatchTest, NoteOnProducesAudio) {
    PRINT_TEST_HEADER(
        "Rain — Smoke",
        "LP+HPF filtered noise with slow ADSR produces non-silent audio after attack.",
        "engine_load_patch(rain.json) → note_on → skip 55 blocks → measure 10",
        "RMS > 0.001 after attack completes (~587ms).",
        sample_rate
    );

    ASSERT_EQ(engine_load_patch(engine(), kPatch), 0);
    engine_note_on(engine(), 60, 1.0f);

    const size_t FRAMES = 512;
    std::vector<float> buf(FRAMES * 2);

    for (int b = 0; b < 55; ++b)
        engine_process(engine(), buf.data(), FRAMES);

    double sum_sq = 0.0;
    for (int b = 0; b < 10; ++b) {
        engine_process(engine(), buf.data(), FRAMES);
        for (size_t i = 0; i < FRAMES; ++i) sum_sq += double(buf[i * 2]) * double(buf[i * 2]);
    }
    engine_note_off(engine(), 60);

    float rms = float(std::sqrt(sum_sq / double(FRAMES * 10)));
    std::cout << "[Rain] Post-attack RMS (blocks 55–65): " << rms << "\n";
    EXPECT_GT(rms, 0.001f) << "Expected non-silent rain texture after 0.5s attack";
}

// ---------------------------------------------------------------------------
// Test 2: SlowAttack — RMS grows during the 0.5s attack phase
//
// attack=0.5s ≈ 46.9 blocks.
// At blocks 5–10 (~53–107ms): ≈ 24–42% of plateau.
// At blocks 50–60 (~533–640ms): ≈ plateau (attack complete).
// Expected: rms_late > rms_early × 1.5
// ---------------------------------------------------------------------------

TEST_F(RainPatchTest, SlowAttack) {
    PRINT_TEST_HEADER(
        "Rain — Slow Attack (automated)",
        "ADSR attack=0.5s: RMS at blocks 5–10 (~53–107ms) is noticeably less "
        "than RMS at blocks 50–60 (~533–640ms, attack complete).",
        "engine_load_patch → note_on → 60 blocks → compare early vs late RMS",
        "rms_late > rms_early × 1.5",
        sample_rate
    );

    ASSERT_EQ(engine_load_patch(engine(), kPatch), 0);
    engine_note_on(engine(), 60, 1.0f);

    const size_t FRAMES      = 512;
    const int    EARLY_START = 5;
    const int    EARLY_END   = 10;  // ~53–107ms
    const int    LATE_START  = 50;
    const int    LATE_END    = 60;  // ~533–640ms

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

    std::cout << "[Rain] RMS early (~53–107ms):   " << rms_early << "\n";
    std::cout << "[Rain] RMS late  (~533–640ms):  " << rms_late  << "\n";

    EXPECT_GT(rms_late, rms_early * 1.5f)
        << "Expected late RMS > 1.5× early RMS for attack=0.5s";
}

// ---------------------------------------------------------------------------
// Test 3: LongSustain — signal remains present well past 1 second
// ---------------------------------------------------------------------------

TEST_F(RainPatchTest, LongSustain) {
    PRINT_TEST_HEADER(
        "Rain — Long Sustain (automated)",
        "ADSR sustain=1.0: rain texture stays at plateau level at t≈1.07s.",
        "engine_load_patch → note_on → skip 100 blocks → measure 10 blocks",
        "RMS > 0.001 at t≈1.07s",
        sample_rate
    );

    ASSERT_EQ(engine_load_patch(engine(), kPatch), 0);
    engine_note_on(engine(), 60, 1.0f);

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
    std::cout << "[Rain] Sustain RMS at t≈1.07s: " << rms << "\n";
    EXPECT_GT(rms, 0.001f) << "Expected rain texture to remain sustained at 1s";
}

// ---------------------------------------------------------------------------
// Test 4: Audible — 6-second rain wash demonstrating full envelope arc
// ---------------------------------------------------------------------------

TEST_F(RainPatchTest, RainWashAudible) {
    PRINT_TEST_HEADER(
        "Rain — Atmospheric Wash (audible)",
        "Continuous rain texture with 0.5s fade-in and 2.0s fade-out. "
        "Hear the broadband LP+HPF filtered noise swell and dissipate.",
        "engine_load_patch(rain.json) → engine_start → note_on → 4s → note_off → fade",
        "Audible rain-like noise texture with slow atmospheric envelope.",
        sample_rate
    );

    ASSERT_EQ(engine_load_patch(engine(), kPatch), 0);
    ASSERT_ENGINE_START(engine());

    std::cout << "[Rain] Starting rain — 0.5s fade in…\n";
    engine_note_on(engine(), 60, 1.0f);
    std::this_thread::sleep_for(std::chrono::milliseconds(4000));

    std::cout << "[Rain] Stopping — 2.0s fade out…\n";
    engine_note_off(engine(), 60);
    std::this_thread::sleep_for(std::chrono::milliseconds(3000));

    engine_stop(engine());
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
