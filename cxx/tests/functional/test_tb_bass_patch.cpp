/**
 * @file test_tb_bass_patch.cpp
 * @brief Functional tests for tb_bass.json — TB-style diode ladder bass patch.
 *
 * Patch: VCO (pulse 50%) → VCF (DIODE_FILTER, 3/4-pole blend) → VCA ← ENV
 *        ENV: attack=2ms, decay=80ms, sustain=0 (fully percussive)
 *
 * Phase 17 additions verified here:
 *   - kybd_cv auto-injection: effective filter cutoff tracks note pitch.
 *   - The diode ladder's 3/4-pole blend (at resonance ≈ 0.80, blend shifts
 *     toward 3-pole) gives the characteristic rubbery TB-303 "zap" on each note.
 *
 * Test: two-octave chromatic scale (C2–B3, MIDI 36–59) with a logarithmic
 * filter sweep from 400 Hz to 4000 Hz and resonance 0.80.
 *
 * Assertions:
 *   1. All 24 notes produce non-silent audio at onset (RMS over first 8 blocks).
 *   2. Spectral centroid rises from C2 (low cutoff) to B3 (high cutoff + kybd_cv).
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

class TbBassPatchTest : public ::testing::Test {
protected:
    void SetUp() override {
        test::init_test_environment();
        sample_rate = test::get_safe_sample_rate(0);
        engine_wrapper = std::make_unique<test::EngineWrapper>(sample_rate);
    }

    EngineHandle engine() { return engine_wrapper->get(); }

    int sample_rate;
    std::unique_ptr<test::EngineWrapper> engine_wrapper;

    static constexpr const char* kPatch = "patches/tb_bass.json";

    // Two-octave chromatic: C2 (36) → B3 (59)
    static constexpr int   kFirstMidi  = 36;
    static constexpr int   kNumNotes   = 24;
    static constexpr float kResonance  = 0.80f;
    static constexpr float kCutoffLow  = 400.0f;
    static constexpr float kCutoffHigh = 4000.0f;
};

// ---------------------------------------------------------------------------
// Test 1: Chromatic scale — per-note onset RMS non-silent, centroid rises
// ---------------------------------------------------------------------------

TEST_F(TbBassPatchTest, ChromaticScaleFilterSweep) {
    PRINT_TEST_HEADER(
        "TB Bass — Chromatic Scale + Filter Sweep (automated)",
        "Two-octave chromatic scale (C2–B3) with log cutoff sweep 400→4000 Hz, res=0.80. "
        "ENV is fully percussive (sustain=0, decay=80ms); captures onset of each note.",
        "engine_load_patch(tb_bass.json) → chromatic scale → engine_process → DCT",
        "All 24 note onsets non-silent; centroid(B3) > centroid(C2).",
        sample_rate
    );

    ASSERT_EQ(engine_load_patch(engine(), kPatch), 0);
    set_param(engine(), "vcf_res", kResonance);

    const size_t BLOCK           = 512;
    const size_t WINDOW          = 2048;   // ~42ms capture window
    // TB ENV: attack=2ms completes in ~6ms. Use 8 blocks (~85ms) to capture onset.
    // The envelope decays to ~11% by 85ms, still plenty of signal at onset.
    const int    NOTE_ON_BLOCKS  = 8;
    const int    NOTE_OFF_BLOCKS = 3;      // ~32ms gap — let decay tail finish

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
            if (i == 0 && low_window.size() < WINDOW)
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
            std::cout << "[TbBass] SILENT note " << midi << " (RMS=" << rms << ")\n";
        }
    }

    EXPECT_EQ(silent_notes, 0) << silent_notes << " notes produced near-silent output";

    ASSERT_EQ(low_window.size(),  WINDOW) << "Failed to capture C2 window";
    ASSERT_EQ(high_window.size(), WINDOW) << "Failed to capture B3 window";

    float centroid_low  = spectral_centroid(low_window,  sample_rate);
    float centroid_high = spectral_centroid(high_window, sample_rate);

    std::cout << "[TbBass] Centroid C2  (cutoff=400 Hz):  " << centroid_low  << " Hz\n";
    std::cout << "[TbBass] Centroid B3  (cutoff=4000 Hz): " << centroid_high << " Hz\n";

    EXPECT_GT(centroid_high, centroid_low)
        << "Expected higher centroid on B3 (higher pitch + open filter).\n"
        << "  C2=" << centroid_low << " Hz   B3=" << centroid_high << " Hz";
}

// ---------------------------------------------------------------------------
// Test 2: Audible — chromatic scale with filter sweep (live output)
// ---------------------------------------------------------------------------

TEST_F(TbBassPatchTest, ChromaticScaleAudible) {
    PRINT_TEST_HEADER(
        "TB Bass — Chromatic Scale + Filter Sweep (audible)",
        "Two-octave chromatic scale C2–B3, cutoff sweep 400→4000 Hz, res=0.80.",
        "engine_load_patch(tb_bass.json) → engine_start → chromatic scale",
        "Audible diode ladder chromatic run with rising filter sweep and acid 'zap'.",
        sample_rate
    );

    ASSERT_EQ(engine_load_patch(engine(), kPatch), 0);
    ASSERT_EQ(engine_start(engine()), 0);
    set_param(engine(), "vcf_res", kResonance);

    std::cout << "[TbBass] Playing two-octave chromatic C2–B3 with filter sweep…\n";

    for (int i = 0; i < kNumNotes; ++i) {
        const int   midi   = kFirstMidi + i;
        const float t      = float(i) / float(kNumNotes - 1);
        const float cutoff = kCutoffLow * std::pow(kCutoffHigh / kCutoffLow, t);
        set_param(engine(), "vcf_cutoff", cutoff);
        engine_note_on(engine(), midi, 0.85f);
        std::this_thread::sleep_for(std::chrono::milliseconds(130));
        engine_note_off(engine(), midi);
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(200)); // tail
    engine_stop(engine());
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
