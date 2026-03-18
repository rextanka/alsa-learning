/**
 * @file test_dog_whistle_patch.cpp
 * @brief Functional tests for dog_whistle.json (Phase 17 multi-ADSR dispatch fix).
 *
 * Patch topology:
 *   VCO (sine) → VCA ← ENV_AMP  (attack=10ms, sustain=1.0, release=120ms)
 *                ↑ pitch_cv
 *           ENV_PITCH            (attack=180ms, decay=0, sustain=0.0, release=40ms)
 *
 * Before Phase 17, gate_on/gate_off used find_by_tag("ENV") which matched
 * neither "ENV_PITCH" nor "ENV_AMP", so both envelopes stayed idle and the
 * patch produced silence. After the fix, on_note_on/on_note_off dispatch to all
 * mod_sources, so both ADSRs trigger correctly.
 *
 * The "dog whistle" effect: at note-on, ENV_PITCH attacks over 180ms (pitch
 * rises by up to +1 octave), then immediately drops to sustain=0 (decay=0).
 * VCO pitch_cv = envelope_out, so the VCO sweeps one octave up then snaps back
 * to the base frequency — the characteristic upward-glide whistle shape.
 *
 * Tests (options 1, 2):
 *   1. Smoke            — patch loads, note-on produces non-silent audio.
 *   2. Pitch env fires  — DCT pitch detection on early (~150ms) vs late (~250ms)
 *                         windows confirms pitch_early > pitch_late, verifying
 *                         ENV_PITCH is modulating the VCO.
 */

#include <gtest/gtest.h>
#include "../TestHelper.hpp"
#include "../../src/dsp/analysis/DctProcessor.hpp"
#include "../../src/dsp/analysis/PitchDetector.hpp"
#include <vector>
#include <cmath>

// ---------------------------------------------------------------------------
// Fixture
// ---------------------------------------------------------------------------

class DogWhistlePatchTest : public ::testing::Test {
protected:
    void SetUp() override {
        test::init_test_environment();
        sample_rate = test::get_safe_sample_rate(0);
        engine_wrapper = std::make_unique<test::EngineWrapper>(sample_rate);
    }

    EngineHandle engine() { return engine_wrapper->get(); }

    int sample_rate;
    std::unique_ptr<test::EngineWrapper> engine_wrapper;

    static constexpr const char* kPatch = "patches/dog_whistle.json";
};

// ---------------------------------------------------------------------------
// Test 1: Smoke — patch loads and produces non-silent audio on note-on
// ---------------------------------------------------------------------------

TEST_F(DogWhistlePatchTest, NoteOnProducesAudio) {
    PRINT_TEST_HEADER(
        "Dog Whistle — Smoke",
        "engine_load_patch succeeds; note-on produces non-silent audio via ENV_AMP.",
        "engine_load_patch(dog_whistle.json) → note_on(C4) → engine_process × 20",
        "RMS > 0.001 across 20 × 512-frame blocks after note-on.",
        sample_rate
    );

    ASSERT_EQ(engine_load_patch(engine(), kPatch), 0)
        << "Failed to load " << kPatch;

    engine_note_on(engine(), 60, 1.0f);  // C4

    const size_t FRAMES = 512;
    std::vector<float> out(FRAMES * 2, 0.0f);
    double sum_sq = 0.0;
    for (int b = 0; b < 20; ++b) {
        engine_process(engine(), out.data(), FRAMES);
        for (float s : out) sum_sq += double(s) * double(s);
    }
    float rms = float(std::sqrt(sum_sq / double(FRAMES * 2 * 20)));
    std::cout << "[DogWhistle] Note-on RMS: " << rms << "\n";
    EXPECT_GT(rms, 0.001f) << "Expected non-silent audio — ENV_AMP may not be firing";

    engine_note_off(engine(), 60);
}

// ---------------------------------------------------------------------------
// Test 2: Pitch envelope fires — DCT pitch detection confirms upward glide
//
// The ADSR uses a one-pole IIR (coeff = exp(-log9 / (T·sr))), so attack_time is
// a time constant, not a linear ramp. For attack=0.18s, the envelope reaches 1.0
// at ≈566ms. Decay=0 clamps to 0.001s → drops to sustain=0 in ~3ms. Timeline:
//   0–566ms:  pitch_cv rises 0→1  (freq sweeps up to base*2^1)
//   ~566ms:   pitch_cv snaps to 0  (freq returns to base)
//   566ms+:   pitch_cv = 0         (ENV_AMP sustain=1 keeps amplitude alive)
//
// "Early" window starts at ~128ms: pitch_cv ≈ 0.79, freq ≈ 1.73× base ≈ 453 Hz.
// "Late"  window starts at ~693ms: past peak, pitch_cv = 0, freq = base ≈ 262 Hz.
//
// Before Phase 17 fix both windows would show the same pitch (idle envelopes).
// After fix: pitch_early > pitch_late.
// ---------------------------------------------------------------------------

TEST_F(DogWhistlePatchTest, PitchEnvelopeFiresAndReturns) {
    PRINT_TEST_HEADER(
        "Dog Whistle — Pitch Envelope Fires (automated)",
        "ENV_PITCH modulates VCO pitch_cv: detected frequency is higher early (~150ms) "
        "than late (~700ms, after envelope has peaked and snapped to sustain=0).",
        "engine_load_patch → note_on(C4) → engine_process → DCT PitchDetector (early vs late)",
        "pitch_early > pitch_late (glide up confirmed, base pitch restored after ~566ms peak).",
        sample_rate
    );

    ASSERT_EQ(engine_load_patch(engine(), kPatch), 0);

    engine_note_on(engine(), 60, 1.0f);  // C4 = 261.63 Hz

    const size_t BLOCK        = 512;
    const size_t WINDOW       = 2048;    // ~42ms at 48kHz; ~46ms at 44.1kHz
    const int    EARLY_START  = 12;      // ~128ms  — pitch_cv ≈ 0.79, freq ≈ 1.73× base
    const int    LATE_START   = 65;      // ~693ms  — past peak (~566ms), pitch_cv = 0

    std::vector<float> early_mono, late_mono;
    early_mono.reserve(WINDOW);
    late_mono.reserve(WINDOW);

    std::vector<float> buf(BLOCK * 2);

    const int TOTAL = LATE_START + int(WINDOW / BLOCK) + 2;
    for (int b = 0; b < TOTAL; ++b) {
        engine_process(engine(), buf.data(), BLOCK);
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

    audio::DctProcessor dct(WINDOW, WINDOW);
    std::vector<float> mags_early(WINDOW), mags_late(WINDOW);
    dct.process(early_mono, mags_early);
    dct.process(late_mono,  mags_late);

    float pitch_early = audio::PitchDetector::detect(mags_early, float(sample_rate));
    float pitch_late  = audio::PitchDetector::detect(mags_late,  float(sample_rate));

    std::cout << "[DogWhistle] Pitch early (~150ms): " << pitch_early << " Hz\n";
    std::cout << "[DogWhistle] Pitch late  (~700ms): " << pitch_late  << " Hz\n";

    ASSERT_GT(pitch_early, 10.0f) << "Early window has no detectable pitch — ENV_AMP may not be firing";
    ASSERT_GT(pitch_late,  10.0f) << "Late window has no detectable pitch";

    EXPECT_GT(pitch_early, pitch_late)
        << "Expected pitch to be higher in the early window (ENV_PITCH still attacking)\n"
        << "  early=" << pitch_early << " Hz  late=" << pitch_late << " Hz\n"
        << "  If both are equal, ENV_PITCH dispatch may still be broken";
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
