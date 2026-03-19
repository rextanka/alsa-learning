/**
 * @file test_whistling_patch.cpp
 * @brief Functional tests for whistling.json — pitch-glide sine voice.
 *
 * Patch topology (Practical Synthesis Vol.1 §3-1, Fig 3-5/3-6 pitch glide):
 *   AD_ENVELOPE(PITCH_ENV, attack=1ms, decay=0.4s)
 *       → INVERTER (scale=-0.3) → VCO.pitch_cv   [pitch glide path]
 *   COMPOSITE_GENERATOR(VCO, sine) → SH_FILTER (cutoff=4000 Hz, res=0.08)
 *       → ADSR_ENVELOPE(AMP_ENV, attack=80ms, sustain=0.9, release=150ms) → VCA
 *
 * The pitch-glide technique: at note_on the PITCH_ENV fires instantly,
 * reaching peak in 1ms, then decays over 0.4s.  The INVERTER maps this
 * peak-to-zero ramp to a (−0.3) → 0 pitch offset, so the VCO starts
 * 0.3 octaves (~3 semitones) below the target pitch and slides up to it
 * over 400ms — exactly the way a human whistler "finds" the pitch.
 *
 * Key assertions:
 *   1. Smoke       — note_on produces audio after 80ms AMP_ENV attack.
 *   2. SoftAttack  — RMS grows: early (blocks 2–4) < late (blocks 9–12).
 *   3. PitchGlide  — spectral centroid rises over time: centroid at blocks
 *                    4–6 (pitch below target) < centroid at blocks 45–50
 *                    (PITCH_ENV decayed, pitch at target).
 *   4. Audible     — ascending phrase G4/A4/B4/C5 to hear the pitch glide
 *                    on each note onset.
 *
 * Timing at 48 kHz, block=512 (~10.67ms/block):
 *   AMP_ENV attack=80ms ≈ 7.5 blocks.
 *   PITCH_ENV decay=0.4s ≈ 37.5 blocks.
 *   "pitch at target" window: blocks 45–50 (~480–533ms)
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

class WhistlingPatchTest : public ::testing::Test {
protected:
    void SetUp() override {
        test::init_test_environment();
        sample_rate = test::get_safe_sample_rate(0);
        engine_wrapper = std::make_unique<test::EngineWrapper>(sample_rate);
    }

    EngineHandle engine() { return engine_wrapper->get(); }

    int sample_rate;
    std::unique_ptr<test::EngineWrapper> engine_wrapper;

    static constexpr const char* kPatch = "patches/whistling.json";
};

// ---------------------------------------------------------------------------
// Test 1: Smoke — audio after AMP_ENV attack completes (≈80ms)
// ---------------------------------------------------------------------------

TEST_F(WhistlingPatchTest, NoteOnProducesAudio) {
    PRINT_TEST_HEADER(
        "Whistling — Smoke",
        "ADSR attack=80ms: signal should be present after attack completes.",
        "engine_load_patch(whistling.json) → note_on(C5) → skip 10 blocks → measure 5",
        "RMS > 0.001 after attack",
        sample_rate
    );

    ASSERT_EQ(engine_load_patch(engine(), kPatch), 0);
    engine_note_on(engine(), 72, 1.0f);  // C5

    const size_t FRAMES = 512;
    std::vector<float> buf(FRAMES * 2);

    // Skip first 10 blocks (~107ms) to clear the attack
    for (int b = 0; b < 10; ++b)
        engine_process(engine(), buf.data(), FRAMES);

    double sum_sq = 0.0;
    for (int b = 0; b < 5; ++b) {
        engine_process(engine(), buf.data(), FRAMES);
        for (size_t i = 0; i < FRAMES; ++i) sum_sq += double(buf[i * 2]) * double(buf[i * 2]);
    }
    engine_note_off(engine(), 72);

    float rms = float(std::sqrt(sum_sq / double(FRAMES * 5)));
    std::cout << "[Whistling] RMS (blocks 10–15): " << rms << "\n";
    EXPECT_GT(rms, 0.001f) << "Expected audio after 80ms attack completes";
}

// ---------------------------------------------------------------------------
// Test 2: SoftAttack — RMS grows during AMP_ENV attack phase
//
// attack=80ms ≈ 7.5 blocks.  At block 3 (32ms) ≈ 40% of attack; at block 10
// (107ms) envelope is fully open.
// Expected: rms_late > rms_early
// ---------------------------------------------------------------------------

TEST_F(WhistlingPatchTest, SoftAttack) {
    PRINT_TEST_HEADER(
        "Whistling — Soft Attack (automated)",
        "ADSR attack=80ms: RMS at blocks 2–4 (~20–43ms) < RMS at blocks 9–12 (~96–128ms).",
        "engine_load_patch → note_on(C5) → 12 blocks → compare early vs late window RMS",
        "rms_late > rms_early",
        sample_rate
    );

    ASSERT_EQ(engine_load_patch(engine(), kPatch), 0);
    engine_note_on(engine(), 72, 1.0f);  // C5

    const size_t FRAMES      = 512;
    const int    EARLY_START = 2;
    const int    EARLY_END   = 5;   // ~20–53ms (mid attack)
    const int    LATE_START  = 9;
    const int    LATE_END    = 13;  // ~96–139ms (attack complete)

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
    engine_note_off(engine(), 72);

    float rms_early = float(std::sqrt(early_sq / double(FRAMES * early_n)));
    float rms_late  = float(std::sqrt(late_sq  / double(FRAMES * late_n)));

    std::cout << "[Whistling] RMS early (~20–53ms):    " << rms_early << "\n";
    std::cout << "[Whistling] RMS late  (~96–139ms):   " << rms_late  << "\n";

    EXPECT_GT(rms_late, rms_early)
        << "Expected AMP_ENV attack=80ms to show growing RMS from early to late window";
}

// ---------------------------------------------------------------------------
// Test 3: PitchGlide — spectral centroid rises as PITCH_ENV decays
//
// At onset: PITCH_ENV≈peak → INV output ≈ −0.3 → VCO is 0.3 oct below target.
// At ~500ms: PITCH_ENV≈0 → INV output ≈ 0 → VCO at target pitch.
//
// For C5 (523.25 Hz): pitch-below at onset ≈ 523.25 × 2^(−0.3) ≈ 425 Hz.
// Centroid of pure sine at 425 Hz vs 523 Hz — ratio ≈ 1.23, so threshold 1.1
// gives comfortable margin.
//
// onset window:  blocks 8–12   (~85–128ms, AMP_ENV open, PITCH_ENV still active)
// target window: blocks 46–50  (~490–533ms, PITCH_ENV ≈ decayed to near zero)
// ---------------------------------------------------------------------------

TEST_F(WhistlingPatchTest, PitchGlide) {
    PRINT_TEST_HEADER(
        "Whistling — Pitch Glide (automated)",
        "AD PITCH_ENV → INVERTER: centroid at onset (pitch below) < centroid after glide.",
        "engine_load_patch → note_on(C5) → capture onset centroid, late centroid",
        "centroid_late > centroid_onset × 1.05",
        sample_rate
    );

    ASSERT_EQ(engine_load_patch(engine(), kPatch), 0);
    engine_note_on(engine(), 72, 1.0f);  // C5 = 523.25 Hz

    const size_t FRAMES = 512;
    const size_t WINDOW = 2048;
    std::vector<float> buf(FRAMES * 2);
    std::vector<float> onset_win, late_win;
    onset_win.reserve(WINDOW);
    late_win.reserve(WINDOW);

    for (int b = 0; b < 51; ++b) {
        engine_process(engine(), buf.data(), FRAMES);
        // onset window: blocks 8–12 (AMP_ENV open, PITCH_ENV still pulling pitch down)
        if (b >= 8  && b < 13 && onset_win.size() < WINDOW)
            for (size_t i = 0; i < FRAMES && onset_win.size() < WINDOW; ++i)
                onset_win.push_back(buf[i * 2]);
        // late window: blocks 46–50 (PITCH_ENV decayed, pitch at target)
        if (b >= 46 && b < 51 && late_win.size() < WINDOW)
            for (size_t i = 0; i < FRAMES && late_win.size() < WINDOW; ++i)
                late_win.push_back(buf[i * 2]);
    }
    engine_note_off(engine(), 72);

    if (onset_win.size() < WINDOW) onset_win.resize(WINDOW, 0.0f);
    if (late_win.size()  < WINDOW) late_win.resize(WINDOW, 0.0f);

    float centroid_onset = spectral_centroid(onset_win, sample_rate);
    float centroid_late  = spectral_centroid(late_win,  sample_rate);

    std::cout << "[Whistling] Centroid onset (~85–128ms):   " << centroid_onset << " Hz\n";
    std::cout << "[Whistling] Centroid late  (~490–533ms):  " << centroid_late  << " Hz\n";
    std::cout << "[Whistling] Ratio late/onset:             "
              << centroid_late / centroid_onset << "\n";

    EXPECT_GT(centroid_late, centroid_onset * 1.05f)
        << "Expected pitch glide to raise centroid as PITCH_ENV decays to zero";
}

// ---------------------------------------------------------------------------
// Test 4: Audible — ascending phrase G4/A4/B4/C5 (hear the glide on each onset)
// ---------------------------------------------------------------------------

TEST_F(WhistlingPatchTest, AscendingPhraseAudible) {
    PRINT_TEST_HEADER(
        "Whistling — Ascending Phrase (audible)",
        "Sine voice with pitch-below-target onset glide on each note.",
        "engine_load_patch(whistling.json) → engine_start → G4/A4/B4/C5",
        "Audible singing pitch-glide effect — each note slides up to pitch.",
        sample_rate
    );

    ASSERT_EQ(engine_load_patch(engine(), kPatch), 0);
    ASSERT_ENGINE_START(engine());

    constexpr int NOTE_MS    = 700;  // long enough to hear both glide and sustain
    constexpr int RELEASE_MS = 300;  // 150ms release + margin

    const int notes[] = {67, 69, 71, 72};  // G4, A4, B4, C5
    std::cout << "[Whistling] Playing G4 → A4 → B4 → C5 (hear pitch glide on each onset)…\n";
    for (int midi : notes) {
        engine_note_on(engine(), midi, 0.9f);
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
