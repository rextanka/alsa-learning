/**
 * @file test_harpsichord_patch.cpp
 * @brief Functional tests for harpsichord.json (Phase 17 INVERTER fix).
 *
 * Patch topology:
 *   VCO (pulse) → VCF (MOOG_FILTER, cutoff=1200 Hz) → VCA ← ENV (attack=1ms, decay=500ms)
 *                                                ↑ cutoff_cv
 *                                    INV (scale=-0.6) ← ENV
 *
 * Before Phase 17, InverterProcessor.do_pull() returned a constant -0.6 DC value,
 * so the filter was stuck at 1200 * 2^(-0.6) ≈ 792 Hz. After the fix, the INVERTER
 * maps the decaying envelope (1→0 over 500ms) to cutoff_cv (-0.6→0), so the filter
 * opens from ~792 Hz to 1200 Hz over the note's decay.
 *
 * Tests (options 1, 2, 4 selected by user):
 *   1. Smoke         — patch and Handel Gavotte MIDI load without error.
 *   2. RMS           — MIDI playback produces non-trivial signal throughout.
 *   3. Centroid rise — spectral centroid increases over decay, confirming the
 *                      INVERTER is driving the filter sweep (DCT-based analysis).
 *   4. Audible       — full Gavotte played live through harpsichord patch.
 */

#include <gtest/gtest.h>
#include "../TestHelper.hpp"
#include "PatchAnalysis.hpp"
#include <vector>
#include <fstream>
#include <cmath>

// ---------------------------------------------------------------------------
// Fixture
// ---------------------------------------------------------------------------

class HarpsichordPatchTest : public ::testing::Test {
protected:
    void SetUp() override {
        test::init_test_environment();
        sample_rate = test::get_safe_sample_rate(0);
        engine_wrapper = std::make_unique<test::EngineWrapper>(sample_rate);
    }

    EngineHandle engine() { return engine_wrapper->get(); }

    int sample_rate;
    std::unique_ptr<test::EngineWrapper> engine_wrapper;

    static constexpr const char* kPatch = "patches/harpsichord.json";
    static constexpr const char* kMidi  = "midi/handel/hwv491_gavotte.mid";
};

// ---------------------------------------------------------------------------
// Test 1: Smoke — patch and MIDI load without error
// ---------------------------------------------------------------------------

TEST_F(HarpsichordPatchTest, LoadPatchAndMidi) {
    PRINT_TEST_HEADER(
        "Harpsichord — Smoke",
        "engine_load_patch and engine_load_midi succeed for harpsichord + Handel Gavotte.",
        "engine_load_patch(harpsichord.json) + engine_load_midi(hwv491_gavotte.mid)",
        "Both return 0.",
        sample_rate
    );

    ASSERT_EQ(engine_load_patch(engine(), kPatch), 0)
        << "Failed to load " << kPatch;

    if (std::ifstream f(kMidi); !f.good()) {
        GTEST_SKIP() << kMidi << " not found — skipping MIDI load check";
    }
    EXPECT_EQ(engine_load_midi(engine(), kMidi), 0);
}

// ---------------------------------------------------------------------------
// Test 2: Non-trivial RMS — MIDI playback produces signal
// ---------------------------------------------------------------------------

TEST_F(HarpsichordPatchTest, MidiPlaybackRmsNonTrivial) {
    PRINT_TEST_HEADER(
        "Harpsichord — RMS Non-Trivial",
        "MIDI playback of Handel Gavotte through harpsichord patch produces non-silent audio.",
        "engine_load_patch → engine_load_midi → engine_midi_play → engine_process (seek first note)",
        "RMS > 0.001 across 128 blocks after first MIDI note fires.",
        sample_rate
    );

    if (std::ifstream f(kMidi); !f.good()) {
        GTEST_SKIP() << kMidi << " not found";
    }

    ASSERT_EQ(engine_load_patch(engine(), kPatch), 0);
    ASSERT_EQ(engine_load_midi(engine(), kMidi), 0);
    engine_midi_play(engine());

    const size_t FRAMES = 512;
    std::vector<float> out(FRAMES * 2, 0.0f);

    // Skip up to 256 blocks (~2.7s at 48kHz/512) to reach the first note,
    // then require non-zero RMS within the following 128 blocks (~1.4s).
    double sum_sq   = 0.0;
    int    counted  = 0;
    bool   got_note = false;
    for (int b = 0; b < 384; ++b) {
        engine_process(engine(), out.data(), FRAMES);
        if (!got_note) {
            // Look for first non-trivial block to confirm notes have started
            for (float s : out) { if (std::abs(s) > 0.0001f) { got_note = true; break; } }
        }
        if (got_note) {
            for (float s : out) sum_sq += double(s) * double(s);
            ++counted;
            if (counted >= 128) break;
        }
    }
    ASSERT_TRUE(got_note) << "No audio produced within 256 blocks — MIDI may not be firing";
    float rms = float(std::sqrt(sum_sq / double(FRAMES * 2 * counted)));
    std::cout << "[Harpsichord] Gavotte playback RMS: " << rms
              << " (over " << counted << " blocks after first note)\n";
    EXPECT_GT(rms, 0.001f) << "Expected non-silent output during MIDI playback";

    engine_midi_stop(engine());
}

// ---------------------------------------------------------------------------
// Test 3: Filter sweep — spectral centroid rises over note decay
//
// ENV: attack=1ms, decay=500ms, sustain=0. INVERTER scale=-0.6.
// At attack peak:  cutoff_cv ≈ -0.6  →  effective cutoff ≈ 1200 * 2^(-0.6) ≈ 792 Hz
// At 350ms:        cutoff_cv ≈ -0.18 →  effective cutoff ≈ 1200 * 2^(-0.18) ≈ 1060 Hz
//
// We capture a 2048-sample "early" window (~50ms) and a "late" window (~350ms),
// compute DCT spectral centroid for each, and assert late > early.
// ---------------------------------------------------------------------------

TEST_F(HarpsichordPatchTest, FilterSweepCentroidRises) {
    PRINT_TEST_HEADER(
        "Harpsichord — INVERTER Filter Sweep (automated)",
        "Spectral centroid rises over note decay as INVERTER opens the Moog filter cutoff.",
        "engine_load_patch → note_on → engine_process → DCT centroid (early vs late)",
        "centroid_late > centroid_early (filter opens as ENV decays from 1 to 0).",
        sample_rate
    );

    ASSERT_EQ(engine_load_patch(engine(), kPatch), 0);

    engine_note_on(engine(), 60, 1.0f);  // C4

    const size_t BLOCK          = 512;
    const size_t WINDOW         = 2048;   // ~46 ms at 44100 Hz
    const int    EARLY_START    = 2;      // skip 2 blocks (~23ms), past 1ms attack
    const int    LATE_START     = 30;     // ~348ms into decay

    std::vector<float> early_mono, late_mono;
    early_mono.reserve(WINDOW);
    late_mono.reserve(WINDOW);

    std::vector<float> buf(BLOCK * 2);

    const int TOTAL = LATE_START + int(WINDOW / BLOCK) + 2;
    for (int b = 0; b < TOTAL; ++b) {
        engine_process(engine(), buf.data(), BLOCK);
        // Left channel (even indices) into whichever window is still filling
        for (size_t i = 0; i < BLOCK; ++i) {
            if (b >= EARLY_START && early_mono.size() < WINDOW)
                early_mono.push_back(buf[i * 2]);
            else if (b >= LATE_START && late_mono.size() < WINDOW)
                late_mono.push_back(buf[i * 2]);
        }
    }

    engine_note_off(engine(), 60);

    ASSERT_EQ(early_mono.size(), WINDOW) << "Failed to capture early window";
    ASSERT_EQ(late_mono.size(),  WINDOW) << "Failed to capture late window";

    float centroid_early = spectral_centroid(early_mono, sample_rate);
    float centroid_late  = spectral_centroid(late_mono,  sample_rate);

    std::cout << "[Harpsichord] Centroid early (~50ms):  " << centroid_early << " Hz\n";
    std::cout << "[Harpsichord] Centroid late  (~350ms): " << centroid_late  << " Hz\n";

    EXPECT_GT(centroid_late, centroid_early)
        << "Expected spectral centroid to rise as INVERTER lets filter open during decay\n"
        << "  early=" << centroid_early << " Hz  late=" << centroid_late << " Hz";
}

// ---------------------------------------------------------------------------
// Test 4: Audible — full Handel Gavotte on harpsichord patch (live output)
// ---------------------------------------------------------------------------

TEST_F(HarpsichordPatchTest, GavotteAudible) {
    PRINT_TEST_HEADER(
        "Harpsichord — Handel Gavotte (audible)",
        "Play hwv491_gavotte.mid through harpsichord.json for live listening.",
        "engine_load_patch(harpsichord.json) → engine_start → engine_load_midi → engine_midi_play",
        "Audible baroque harpsichord with INVERTER-driven pluck filter transient (~30s).",
        sample_rate
    );

    if (std::ifstream f(kMidi); !f.good()) {
        GTEST_SKIP() << kMidi << " not found";
    }

    ASSERT_EQ(engine_load_patch(engine(), kPatch), 0);
    ASSERT_EQ(engine_start(engine()), 0);
    ASSERT_EQ(engine_load_midi(engine(), kMidi), 0);
    engine_midi_play(engine());

    std::cout << "[Harpsichord] Playing Handel Gavotte HWV 491 (~30s)…\n";
    std::cout << "[Harpsichord] Source: Mutopia Project, CC0 (public domain)\n";
    test::wait_while_running(35);

    engine_midi_stop(engine());
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
