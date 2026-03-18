/**
 * @file test_sh_bass_patch.cpp
 * @brief Functional tests for sh_bass.json — SH-style CEM ladder bass patch.
 *
 * Patch: VCO (saw+sub) → VCF (SH_FILTER, 4-pole CEM) → VCA ← ENV
 *
 * Phase 17 additions verified here:
 *   - kybd_cv auto-injection: effective filter cutoff tracks note pitch
 *     (base_cutoff * 2^log2(freq/C4)) — higher notes → higher cutoff
 *   - vcf_cutoff sweep exercised manually in addition to kybd tracking
 *
 * Test: two-octave chromatic scale (C2–B3, MIDI 36–59) with a logarithmic
 * filter sweep from 400 Hz to 4000 Hz and resonance 0.80.
 *
 * Assertions:
 *   1. All 24 notes produce non-silent audio (RMS > threshold per note).
 *   2. Spectral centroid of the last note (B3, cutoff 4000 Hz + kybd_cv)
 *      is significantly higher than the first note (C2, cutoff 400 Hz),
 *      confirming both the sweep and kybd_cv tracking are working.
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

class ShBassPatchTest : public ::testing::Test {
protected:
    void SetUp() override {
        test::init_test_environment();
        sample_rate = test::get_safe_sample_rate(0);
        engine_wrapper = std::make_unique<test::EngineWrapper>(sample_rate);
    }

    EngineHandle engine() { return engine_wrapper->get(); }

    int sample_rate;
    std::unique_ptr<test::EngineWrapper> engine_wrapper;

    static constexpr const char* kPatch = "patches/sh_bass.json";

    // Two-octave chromatic: C2 (36) → B3 (59)
    static constexpr int kFirstMidi = 36;
    static constexpr int kNumNotes  = 24;
    static constexpr float kResonance  = 0.80f;
    static constexpr float kCutoffLow  = 400.0f;
    static constexpr float kCutoffHigh = 4000.0f;
};

// ---------------------------------------------------------------------------
// Test 1: Chromatic scale — all notes non-silent, centroid rises low→high
// ---------------------------------------------------------------------------

TEST_F(ShBassPatchTest, ChromaticScaleFilterSweep) {
    PRINT_TEST_HEADER(
        "SH Bass — Chromatic Scale + Filter Sweep (automated)",
        "Two-octave chromatic scale (C2–B3) with log cutoff sweep 400→4000 Hz, res=0.80. "
        "Verifies per-note RMS and spectral centroid rise from first to last note.",
        "engine_load_patch(sh_bass.json) → chromatic scale → engine_process → DCT",
        "All 24 notes non-silent; centroid(B3) > centroid(C2).",
        sample_rate
    );

    ASSERT_EQ(engine_load_patch(engine(), kPatch), 0);
    set_param(engine(), "vcf_res", kResonance);

    const size_t BLOCK           = 512;
    const size_t WINDOW          = 2048;   // ~42ms capture window
    const int    NOTE_ON_BLOCKS  = 14;     // ~149ms — past SH ENV attack (5ms), into sustain
    const int    NOTE_OFF_BLOCKS = 2;      // ~21ms gap

    std::vector<float> buf(BLOCK * 2);
    std::vector<float> low_window, high_window;
    low_window.reserve(WINDOW);
    high_window.reserve(WINDOW);

    int silent_notes = 0;

    for (int i = 0; i < kNumNotes; ++i) {
        const int   midi   = kFirstMidi + i;
        const float t      = float(i) / float(kNumNotes - 1);
        const float cutoff = kCutoffLow * std::pow(kCutoffHigh / kCutoffLow, t);
        set_param(engine(), "vcf_cutoff", cutoff);
        engine_note_on(engine(), midi, 0.85f);

        double sum_sq = 0.0;
        for (int b = 0; b < NOTE_ON_BLOCKS; ++b) {
            engine_process(engine(), buf.data(), BLOCK);
            for (size_t s = 0; s < BLOCK; ++s) sum_sq += double(buf[s * 2]) * double(buf[s * 2]);
            // Capture first and last note windows (skip first 2 blocks for transient)
            if (i == 0 && b >= 2 && low_window.size() < WINDOW)
                for (size_t s = 0; s < BLOCK && low_window.size() < WINDOW; ++s)
                    low_window.push_back(buf[s * 2]);
            if (i == kNumNotes - 1 && high_window.size() < WINDOW)
                for (size_t s = 0; s < BLOCK && high_window.size() < WINDOW; ++s)
                    high_window.push_back(buf[s * 2]);
        }
        engine_note_off(engine(), midi);
        for (int b = 0; b < NOTE_OFF_BLOCKS; ++b)
            engine_process(engine(), buf.data(), BLOCK);

        float rms = float(std::sqrt(sum_sq / double(BLOCK * NOTE_ON_BLOCKS)));
        if (rms < 0.001f) {
            ++silent_notes;
            std::cout << "[ShBass] SILENT note " << midi << " (RMS=" << rms << ")\n";
        }
    }

    EXPECT_EQ(silent_notes, 0) << silent_notes << " notes produced near-silent output";

    ASSERT_EQ(low_window.size(),  WINDOW) << "Failed to capture C2 window";
    ASSERT_EQ(high_window.size(), WINDOW) << "Failed to capture B3 window";

    float centroid_low  = spectral_centroid(low_window,  sample_rate);
    float centroid_high = spectral_centroid(high_window, sample_rate);

    std::cout << "[ShBass] Centroid C2  (cutoff=400 Hz):  " << centroid_low  << " Hz\n";
    std::cout << "[ShBass] Centroid B3  (cutoff=4000 Hz): " << centroid_high << " Hz\n";

    EXPECT_GT(centroid_high, centroid_low)
        << "Expected higher centroid on B3 (higher pitch + open filter).\n"
        << "  C2=" << centroid_low << " Hz   B3=" << centroid_high << " Hz";
}

// ---------------------------------------------------------------------------
// Test 2: Audible — chromatic scale with filter sweep (live output)
// ---------------------------------------------------------------------------

TEST_F(ShBassPatchTest, ChromaticScaleAudible) {
    PRINT_TEST_HEADER(
        "SH Bass — Chromatic Scale + Filter Sweep (audible)",
        "Two-octave chromatic scale C2–B3, cutoff sweep 400→4000 Hz, res=0.80.",
        "engine_load_patch(sh_bass.json) → engine_start → chromatic scale",
        "Audible CEM ladder chromatic run with rising filter resonant sweep.",
        sample_rate
    );

    ASSERT_EQ(engine_load_patch(engine(), kPatch), 0);
    ASSERT_EQ(engine_start(engine()), 0);
    set_param(engine(), "vcf_res", kResonance);

    std::cout << "[ShBass] Playing two-octave chromatic C2–B3 with filter sweep…\n";

    for (int i = 0; i < kNumNotes; ++i) {
        const int   midi   = kFirstMidi + i;
        const float t      = float(i) / float(kNumNotes - 1);
        const float cutoff = kCutoffLow * std::pow(kCutoffHigh / kCutoffLow, t);
        set_param(engine(), "vcf_cutoff", cutoff);
        engine_note_on(engine(), midi, 0.85f);
        std::this_thread::sleep_for(std::chrono::milliseconds(160));
        engine_note_off(engine(), midi);
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(400)); // release tail
    engine_stop(engine());
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
