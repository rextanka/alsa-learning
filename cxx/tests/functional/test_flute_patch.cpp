/**
 * @file test_flute_patch.cpp
 * @brief Functional tests for flute.json.
 *
 * Patch topology:
 *   LFO1 (5 Hz, intensity=0.002) → VCO pitch_cv  (vibrato)
 *   VCO (sine=1.0, tri=0.15, noise=0.12) → VCA ← ENV
 *   ENV: attack=0.1s, decay=0.05s, sustain=0.85, release=0.25s
 *
 * Characteristic features:
 *   - 100ms attack ramp (softer than brass, longer than percussive patches)
 *   - Breath noise (noise_gain=0.12) pushes spectral energy above the fundamental,
 *     raising the DCT centroid well above a pure-sine baseline
 *   - Subtle LFO vibrato (±0.002 V/oct at 5 Hz)
 *
 * Tests:
 *   1. Smoke        — patch loads and note-on produces non-silent audio.
 *   2. AttackRamp   — RMS at ~160–213ms significantly exceeds RMS at ~10–43ms,
 *                    confirming the 100ms ADSR attack is shaping amplitude.
 *   3. BreathNoise  — DCT spectral centroid during sustain is well above the
 *                    played fundamental, confirming noise_gain=0.12 is active.
 *   4. Audible      — a short flute melody (C5–E5–D5–G5).
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

class FlutePatchTest : public ::testing::Test {
protected:
    void SetUp() override {
        test::init_test_environment();
        sample_rate = test::get_safe_sample_rate(0);
        engine_wrapper = std::make_unique<test::EngineWrapper>(sample_rate);
    }

    EngineHandle engine() { return engine_wrapper->get(); }

    int sample_rate;
    std::unique_ptr<test::EngineWrapper> engine_wrapper;

    static constexpr const char* kPatch = "patches/flute.json";
};

// ---------------------------------------------------------------------------
// Test 1: Smoke — patch loads and produces audio
// ---------------------------------------------------------------------------

TEST_F(FlutePatchTest, NoteOnProducesAudio) {
    PRINT_TEST_HEADER(
        "Flute — Smoke",
        "Note-on produces non-silent audio through VCO → VCA chain.",
        "engine_load_patch(flute.json) → note_on → engine_process × 30",
        "RMS > 0.001 across 30 blocks (allowing the 100ms attack to build).",
        sample_rate
    );

    ASSERT_EQ(engine_load_patch(engine(), kPatch), 0);
    engine_note_on(engine(), 72, 1.0f);  // C5

    const size_t FRAMES = 512;
    std::vector<float> buf(FRAMES * 2);
    double sum_sq = 0.0;
    for (int b = 0; b < 30; ++b) {
        engine_process(engine(), buf.data(), FRAMES);
        for (size_t i = 0; i < FRAMES; ++i) sum_sq += double(buf[i * 2]) * double(buf[i * 2]);
    }
    float rms = float(std::sqrt(sum_sq / double(FRAMES * 30)));
    std::cout << "[Flute] 30-block RMS: " << rms << "\n";
    EXPECT_GT(rms, 0.001f) << "Expected non-silent output during note";

    engine_note_off(engine(), 72);
}

// ---------------------------------------------------------------------------
// Test 2: AttackRamp — RMS rises significantly over the 100ms attack
//
// ADSR IIR: coeff = exp(-log(9) / (T * sr))
//   attack=0.1s, sr=48000: level at ~32ms  ≈ 1 - exp(-0.440) ≈ 0.357
//                           level at ~171ms ≈ 1 - exp(-1.880) ≈ 0.847
//
// EARLY window: blocks 1–4  (~10–43ms  — envelope at ~25–36%)
// LATE  window: blocks 15–20 (~160–213ms — envelope past attack, ~84–87%)
//
// Expected ratio late/early ≥ 1.8.
// ---------------------------------------------------------------------------

TEST_F(FlutePatchTest, AttackRampRmsRises) {
    PRINT_TEST_HEADER(
        "Flute — Attack Ramp (automated)",
        "RMS at ~160–213ms is significantly higher than at ~10–43ms, "
        "confirming the 100ms ADSR attack ramp is shaping VCA gain.",
        "engine_load_patch → note_on(C5) → engine_process → compare early vs late RMS",
        "rms_late / rms_early ≥ 1.8",
        sample_rate
    );

    ASSERT_EQ(engine_load_patch(engine(), kPatch), 0);
    engine_note_on(engine(), 72, 1.0f);  // C5

    const size_t FRAMES       = 512;
    const int    EARLY_START  = 1;    // ~10ms
    const int    EARLY_END    = 5;    // ~53ms
    const int    LATE_START   = 15;   // ~160ms — past attack
    const int    LATE_END     = 21;   // ~224ms

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
    float ratio     = rms_early > 1e-6f ? rms_late / rms_early : 0.0f;

    std::cout << "[Flute] RMS early (~10–43ms):   " << rms_early << "\n";
    std::cout << "[Flute] RMS late  (~160–213ms): " << rms_late  << "\n";
    std::cout << "[Flute] Ratio late/early:        " << ratio     << "\n";

    EXPECT_GT(rms_early, 0.0001f) << "No signal in early window — envelope may not be running";
    EXPECT_GT(ratio, 1.8f)
        << "Expected ≥1.8× RMS rise over 100ms attack (got " << ratio << ")";
}

// ---------------------------------------------------------------------------
// Test 3: BreathNoise — noise_gain=0.12 raises spectral centroid above fundamental
//
// A pure sine at C5 (523.25 Hz) produces a DCT centroid near 523 Hz.
// With broadband noise at 12% of amplitude added, energy is spread across
// all frequency bins. The noise contribution is roughly uniform per-bin, so
// the centroid is pulled well above the fundamental.
//
// Conservative threshold: centroid > fundamental × 3 (i.e. > ~1570 Hz for C5).
// ---------------------------------------------------------------------------

TEST_F(FlutePatchTest, BreathNoiseRaisesSpectralCentroid) {
    PRINT_TEST_HEADER(
        "Flute — Breath Noise Spectral Character (automated)",
        "noise_gain=0.12 spreads energy above the fundamental: DCT centroid during "
        "sustain is well above the C5 fundamental (523 Hz).",
        "engine_load_patch → note_on(C5) → skip 25 blocks → capture 2048 samples → centroid",
        "centroid > fundamental × 3 (~1570 Hz)",
        sample_rate
    );

    ASSERT_EQ(engine_load_patch(engine(), kPatch), 0);
    engine_note_on(engine(), 72, 1.0f);  // C5 = 523.25 Hz

    const size_t BLOCK  = 512;
    const size_t WINDOW = 2048;
    std::vector<float> buf(BLOCK * 2);
    std::vector<float> mono;
    mono.reserve(WINDOW);

    // Skip 25 blocks (~267ms) to get past attack (100ms) + decay (50ms) into sustain
    for (int b = 0; b < 30; ++b) {
        engine_process(engine(), buf.data(), BLOCK);
        if (b >= 25) {
            for (size_t i = 0; i < BLOCK && mono.size() < WINDOW; ++i)
                mono.push_back(buf[i * 2]);
        }
    }
    engine_note_off(engine(), 72);

    ASSERT_EQ(mono.size(), WINDOW);

    constexpr float kC5Hz = 523.25f;
    float centroid = spectral_centroid(mono, sample_rate);

    std::cout << "[Flute] C5 fundamental:   " << kC5Hz    << " Hz\n";
    std::cout << "[Flute] Spectral centroid: " << centroid << " Hz\n";
    std::cout << "[Flute] Centroid / fund.:  " << centroid / kC5Hz << "×\n";

    EXPECT_GT(centroid, kC5Hz * 3.0f)
        << "Expected centroid > 1570 Hz; noise_gain=0.12 should push energy well above "
        << "the C5 fundamental. Got " << centroid << " Hz";
}

// ---------------------------------------------------------------------------
// Test 4: Audible — short flute melody
// ---------------------------------------------------------------------------

TEST_F(FlutePatchTest, FlutemelodyAudible) {
    PRINT_TEST_HEADER(
        "Flute — Melody (audible)",
        "Play C5–E5–D5–G5 to hear the 100ms attack ramp, breath noise, and LFO vibrato.",
        "engine_load_patch(flute.json) → engine_start → C5/E5/D5/G5",
        "Audible flute-like notes with soft attack, breath tone, and vibrato.",
        sample_rate
    );

    ASSERT_EQ(engine_load_patch(engine(), kPatch), 0);
    ASSERT_ENGINE_START(engine());

    constexpr int NOTE_MS    = 500;
    constexpr int RELEASE_MS = 400;  // 250ms release + margin

    const int notes[] = {72, 76, 74, 79};  // C5, E5, D5, G5
    std::cout << "[Flute] Playing C5 – E5 – D5 – G5…\n";
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
