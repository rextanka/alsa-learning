/**
 * @file test_wind_surf_patch.cpp
 * @brief Functional tests for wind_surf.json — noise source swept by slow LFO.
 *
 * Patch topology:
 *   WHITE_NOISE → MOOG_FILTER (cutoff=800 Hz, LFO sweeping cutoff_cv) → VCA ← ENV
 *   ADSR: attack=0.8s (slow wind fade-in), sustain=1.0, release=1.5s
 *
 * Wind/surf character in Roland §5-2: noise filtered by a slowly-moving LPF
 * creates convincing oceanic texture.  The LFO (0.25 Hz) slowly sweeps the
 * filter cutoff for the "rolling wave" effect.
 *
 * Key assertions:
 *   1. Smoke         — noise source produces audio after attack completes.
 *   2. SlowAttack    — early-window RMS < late-window RMS (0.8s attack is audible).
 *   3. LfoSweepHeard — two 2048-sample windows 2s apart have different spectral
 *                      centroids (LFO shifts filter position over time).
 *   4. Audible       — sustained "wind" note.
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

class WindSurfPatchTest : public ::testing::Test {
protected:
    void SetUp() override {
        test::init_test_environment();
        sample_rate = test::get_safe_sample_rate(0);
        engine_wrapper = std::make_unique<test::EngineWrapper>(sample_rate);
    }

    EngineHandle engine() { return engine_wrapper->get(); }

    int sample_rate;
    std::unique_ptr<test::EngineWrapper> engine_wrapper;

    static constexpr const char* kPatch = "patches/wind_surf.json";
};

// ---------------------------------------------------------------------------
// Test 1: Smoke — noise produces audio (measured after attack settles)
//
// attack=0.8s.  Skip the first 30 blocks (~320ms) to let the envelope ramp,
// then measure RMS over 20 blocks (through ~530ms).  At 320ms the envelope
// has reached ≈1 − e^(−log99·0.32/0.8) ≈ 30% of peak — enough for RMS>0.0005.
// ---------------------------------------------------------------------------

TEST_F(WindSurfPatchTest, NoiseProducesAudioAfterAttack) {
    PRINT_TEST_HEADER(
        "Wind/Surf — Smoke",
        "WHITE_NOISE + MOOG_FILTER produces non-silent audio after envelope attack.",
        "engine_load_patch(wind_surf.json) → note_on → skip 30 blocks → measure 20 blocks",
        "RMS > 0.0005 (envelope partially open at ~320ms).",
        sample_rate
    );

    ASSERT_EQ(engine_load_patch(engine(), kPatch), 0);
    engine_note_on(engine(), 60, 1.0f);  // pitch ignored for noise patch

    const size_t FRAMES = 512;
    std::vector<float> buf(FRAMES * 2);

    // Skip first 30 blocks (allow attack to ramp up to ~30%)
    for (int b = 0; b < 30; ++b)
        engine_process(engine(), buf.data(), FRAMES);

    double sum_sq = 0.0;
    for (int b = 0; b < 20; ++b) {
        engine_process(engine(), buf.data(), FRAMES);
        for (size_t i = 0; i < FRAMES; ++i) sum_sq += double(buf[i * 2]) * double(buf[i * 2]);
    }
    engine_note_off(engine(), 60);

    float rms = float(std::sqrt(sum_sq / double(FRAMES * 20)));
    std::cout << "[WindSurf] RMS after 30 warm-up blocks: " << rms << "\n";
    EXPECT_GT(rms, 0.0005f) << "Expected noise output after attack partial ramp";
}

// ---------------------------------------------------------------------------
// Test 2: SlowAttack — RMS grows from early (blocks 2–5) to late (blocks 85–90)
//
// attack=0.8s at 48 kHz ≈ 75 blocks of 512.
// Early (blocks 2–5, ~20–53ms): envelope ≈ 5% of peak.
// Late  (blocks 85–90, ~906–960ms): envelope ≈ 99% of peak (attack complete).
// Expected: rms_late > rms_early × 2.
// ---------------------------------------------------------------------------

TEST_F(WindSurfPatchTest, SlowAttack) {
    PRINT_TEST_HEADER(
        "Wind/Surf — Slow Attack (automated)",
        "ADSR attack=0.8s: RMS measured at ~20ms should be < RMS measured at ~900ms.",
        "engine_load_patch → note_on → 90 blocks → compare early vs late RMS",
        "rms_late > rms_early × 2.0",
        sample_rate
    );

    ASSERT_EQ(engine_load_patch(engine(), kPatch), 0);
    engine_note_on(engine(), 60, 1.0f);

    const size_t FRAMES      = 512;
    const int    EARLY_START = 2;
    const int    EARLY_END   = 6;   // ~20–62ms
    const int    LATE_START  = 84;
    const int    LATE_END    = 90;  // ~896–960ms (past attack)

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

    std::cout << "[WindSurf] RMS early (~20–62ms):   " << rms_early << "\n";
    std::cout << "[WindSurf] RMS late  (~896–960ms): " << rms_late  << "\n";

    EXPECT_GT(rms_late, rms_early * 2.0f)
        << "Expected late RMS > 2× early RMS for attack=0.8s";
}

// ---------------------------------------------------------------------------
// Test 3: LfoSweep — two windows 2s apart have distinct spectral centroids
//
// LFO at 0.25 Hz has period=4s.  Two windows at t≈1s and t≈3s are a half-
// period apart, so the LFO has swept from one extreme to the other.
// We require |centroid_A − centroid_B| > 60 Hz.
// ---------------------------------------------------------------------------

TEST_F(WindSurfPatchTest, LfoSweepShiftsCentroid) {
    PRINT_TEST_HEADER(
        "Wind/Surf — LFO Filter Sweep (automated)",
        "LFO (0.25 Hz) sweeps MOOG_FILTER cutoff: spectral centroids 2s apart should differ.",
        "engine_load_patch → note_on → skip 95 blocks (~1s) → capture window A → "
        "skip 190 blocks (~2s) → capture window B → compare centroids",
        "|centroid_A − centroid_B| > 60 Hz",
        sample_rate
    );

    ASSERT_EQ(engine_load_patch(engine(), kPatch), 0);
    engine_note_on(engine(), 60, 1.0f);

    const size_t FRAMES = 512;
    const size_t WINDOW = 2048;
    std::vector<float> buf(FRAMES * 2);
    std::vector<float> win_a, win_b;
    win_a.reserve(WINDOW);
    win_b.reserve(WINDOW);

    // Window A: capture after ~1s (past attack, LFO at initial phase)
    const int WINDOW_A_START_BLOCK = 95;   // ~1012ms
    const int WINDOW_B_START_BLOCK = 285;  // ~3040ms (half LFO period later)

    for (int b = 0; b < WINDOW_B_START_BLOCK + 10; ++b) {
        engine_process(engine(), buf.data(), FRAMES);
        if (b >= WINDOW_A_START_BLOCK && win_a.size() < WINDOW)
            for (size_t i = 0; i < FRAMES && win_a.size() < WINDOW; ++i)
                win_a.push_back(buf[i * 2]);
        if (b >= WINDOW_B_START_BLOCK && win_b.size() < WINDOW)
            for (size_t i = 0; i < FRAMES && win_b.size() < WINDOW; ++i)
                win_b.push_back(buf[i * 2]);
    }
    engine_note_off(engine(), 60);

    ASSERT_EQ(win_a.size(), WINDOW);
    ASSERT_EQ(win_b.size(), WINDOW);

    float centroid_a = spectral_centroid(win_a, sample_rate);
    float centroid_b = spectral_centroid(win_b, sample_rate);
    float diff = std::abs(centroid_a - centroid_b);

    std::cout << "[WindSurf] Centroid at t≈1s: " << centroid_a << " Hz\n";
    std::cout << "[WindSurf] Centroid at t≈3s: " << centroid_b << " Hz\n";
    std::cout << "[WindSurf] |diff|:            " << diff       << " Hz\n";

    EXPECT_GT(diff, 60.0f)
        << "Expected LFO sweep to shift spectral centroid by > 60 Hz";
}

// ---------------------------------------------------------------------------
// Test 4: Audible — sustained wind texture
// ---------------------------------------------------------------------------

TEST_F(WindSurfPatchTest, SustainedWindAudible) {
    PRINT_TEST_HEADER(
        "Wind/Surf — Sustained Texture (audible)",
        "Hold a note for 4s to hear the slow attack, LFO filter sweep, and release tail.",
        "engine_load_patch(wind_surf.json) → engine_start → note_on → 4s → note_off → 2s",
        "Audible surf/wind texture with slow swell and rolling LFO filter motion.",
        sample_rate
    );

    ASSERT_EQ(engine_load_patch(engine(), kPatch), 0);
    ASSERT_ENGINE_START(engine());

    std::cout << "[WindSurf] Holding note (4s) — listen for slow attack and LFO sweep…\n";
    engine_note_on(engine(), 60, 1.0f);
    std::this_thread::sleep_for(std::chrono::milliseconds(4000));
    engine_note_off(engine(), 60);
    std::this_thread::sleep_for(std::chrono::milliseconds(2000)); // release tail

    engine_stop(engine());
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
