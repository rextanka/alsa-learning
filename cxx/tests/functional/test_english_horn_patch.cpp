/**
 * @file test_english_horn_patch.cpp
 * @brief Functional tests for english_horn.json — nasal double-reed timbre.
 *
 * Patch topology (Roland System 100M Fig 1-9):
 *   COMPOSITE_GENERATOR (saw) → SH_FILTER (cutoff=1800 Hz, res=0.5) → VCA ← ENV
 *
 * English Horn has NO HPF — the HPF block in Fig 1-9 is dashed and labelled
 * "(OBOE)", meaning it applies only to the Oboe variant (see oboe.json).
 * Keyboard CV is auto-injected into VCF by the engine (kybd_cv port).
 * ADSR drives VCA amplitude only — no filter modulation.
 * The SH_FILTER LP at resonance 0.5 gives the characteristic hollow,
 * slightly reedy quality of English Horn.
 *
 * Key assertions:
 *   1. Smoke        — note_on + 5 blocks produces audio (30ms attack completes
 *                     well within the first second; measure after 5 blocks ~53ms).
 *   2. NasalFormant — spectral centroid during steady-state is in the 600–2000 Hz
 *                     nasal range (HPF removes bass, LP caps top end).
 *   3. Sustain      — RMS remains above floor after attack has completed.
 *   4. Audible      — ascending scale C4 → E4 → G4 → C5.
 *
 * Timing at 48 kHz, block=512 (~10.67ms/block):
 *   attack=30ms ≈ 2.8 blocks; fully open by block 6.
 *   Measure from block 8 (~85ms) onwards for steady-state assertions.
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

class EnglishHornPatchTest : public ::testing::Test {
protected:
    void SetUp() override {
        test::init_test_environment();
        sample_rate = test::get_safe_sample_rate(0);
        engine_wrapper = std::make_unique<test::EngineWrapper>(sample_rate);
    }

    EngineHandle engine() { return engine_wrapper->get(); }

    int sample_rate;
    std::unique_ptr<test::EngineWrapper> engine_wrapper;

    static constexpr const char* kPatch = "patches/english_horn.json";
};

// ---------------------------------------------------------------------------
// Test 1: Smoke — audio after attack completes (~53ms = 5 blocks)
// ---------------------------------------------------------------------------

TEST_F(EnglishHornPatchTest, NoteOnProducesAudio) {
    PRINT_TEST_HEADER(
        "English Horn — Smoke",
        "ADSR attack=30ms: measuring signal from blocks 3–8 after note_on.",
        "engine_load_patch(english_horn.json) → note_on(A4) → skip 3 blocks → measure 5",
        "RMS > 0.001",
        sample_rate
    );

    ASSERT_EQ(engine_load_patch(engine(), kPatch), 0);
    engine_note_on(engine(), 69, 1.0f);  // A4

    const size_t FRAMES = 512;
    std::vector<float> buf(FRAMES * 2);

    // Skip first 3 blocks (~32ms) to clear the attack
    for (int b = 0; b < 3; ++b)
        engine_process(engine(), buf.data(), FRAMES);

    double sum_sq = 0.0;
    for (int b = 0; b < 5; ++b) {
        engine_process(engine(), buf.data(), FRAMES);
        for (size_t i = 0; i < FRAMES; ++i) sum_sq += double(buf[i * 2]) * double(buf[i * 2]);
    }
    engine_note_off(engine(), 69);

    float rms = float(std::sqrt(sum_sq / double(FRAMES * 5)));
    std::cout << "[EnglishHorn] RMS (blocks 3–8): " << rms << "\n";
    EXPECT_GT(rms, 0.001f) << "Expected audio after 30ms attack";
}

// ---------------------------------------------------------------------------
// Test 2: ResonantFormant — centroid above fundamental
//
// SH_FILTER LP at 1800 Hz, resonance=0.5 adds a resonant peak near cutoff.
// For A4 (440 Hz), the fundamental is at 440 Hz; the LP resonance emphasises
// harmonics around 1800 Hz, lifting the centroid well above the fundamental.
// Without an HPF, the centroid includes the low-end energy from the saw, so
// the lower bound is 440 Hz (fundamental) rather than 600 Hz.
// ---------------------------------------------------------------------------

TEST_F(EnglishHornPatchTest, NasalFormant) {
    PRINT_TEST_HEADER(
        "English Horn — Resonant Formant (automated)",
        "SH_FILTER LP 1800Hz res=0.5: centroid above A4 fundamental (440 Hz).",
        "engine_load_patch → note_on(A4) → skip 10 blocks → capture 2048-sample window → centroid",
        "440 < centroid < 2000 Hz",
        sample_rate
    );

    ASSERT_EQ(engine_load_patch(engine(), kPatch), 0);
    engine_note_on(engine(), 69, 1.0f);  // A4 = 440 Hz

    const size_t FRAMES = 512;
    const size_t WINDOW = 2048;
    std::vector<float> buf(FRAMES * 2);
    std::vector<float> win;
    win.reserve(WINDOW);

    // Skip 10 blocks to reach steady-state sustain
    for (int b = 0; b < 10; ++b)
        engine_process(engine(), buf.data(), FRAMES);

    // Capture 2048 samples
    for (int b = 0; b < 6 && win.size() < WINDOW; ++b) {
        engine_process(engine(), buf.data(), FRAMES);
        for (size_t i = 0; i < FRAMES && win.size() < WINDOW; ++i)
            win.push_back(buf[i * 2]);
    }
    engine_note_off(engine(), 69);

    ASSERT_EQ(win.size(), WINDOW);

    float centroid = spectral_centroid(win, sample_rate);
    std::cout << "[EnglishHorn] A4 fundamental:    440 Hz\n";
    std::cout << "[EnglishHorn] Spectral centroid: " << centroid << " Hz\n";

    EXPECT_GT(centroid, 440.0f)
        << "Expected LP resonance to lift centroid above A4 fundamental (440 Hz)";
    EXPECT_LT(centroid, 2000.0f)
        << "Expected LP (1800 Hz) to cap top end, keeping centroid below 2000 Hz";
}

// ---------------------------------------------------------------------------
// Test 3: Sustain — RMS above floor well into sustain phase
//
// sustain=0.85 → signal stays strong past attack+decay window.
// Measure 10 blocks at t≈640ms (block 60) — should be firmly in sustain.
// ---------------------------------------------------------------------------

TEST_F(EnglishHornPatchTest, SustainedTone) {
    PRINT_TEST_HEADER(
        "English Horn — Sustained Tone (automated)",
        "ADSR sustain=0.85: signal remains above threshold at t≈640ms.",
        "engine_load_patch → note_on(A4) → skip 60 blocks → measure 10 blocks",
        "RMS > 0.002 at ~640ms (sustain held)",
        sample_rate
    );

    ASSERT_EQ(engine_load_patch(engine(), kPatch), 0);
    engine_note_on(engine(), 69, 1.0f);  // A4

    const size_t FRAMES = 512;
    std::vector<float> buf(FRAMES * 2);

    for (int b = 0; b < 60; ++b)
        engine_process(engine(), buf.data(), FRAMES);

    double sum_sq = 0.0;
    for (int b = 0; b < 10; ++b) {
        engine_process(engine(), buf.data(), FRAMES);
        for (size_t i = 0; i < FRAMES; ++i) sum_sq += double(buf[i * 2]) * double(buf[i * 2]);
    }
    engine_note_off(engine(), 69);

    float rms = float(std::sqrt(sum_sq / double(FRAMES * 10)));
    std::cout << "[EnglishHorn] Sustain RMS at t≈640ms: " << rms << "\n";
    EXPECT_GT(rms, 0.002f) << "Expected sustained English Horn above noise floor at 640ms";
}

// ---------------------------------------------------------------------------
// Test 4: Audible — ascending scale C4 → E4 → G4 → C5
// ---------------------------------------------------------------------------

TEST_F(EnglishHornPatchTest, AscendingScaleAudible) {
    PRINT_TEST_HEADER(
        "English Horn — Ascending Scale (audible)",
        "Nasal double-reed timbre across C4/E4/G4/C5.",
        "engine_load_patch(english_horn.json) → engine_start → C4/E4/G4/C5",
        "Audible reedy, nasal tone with series LP+HP filter formant.",
        sample_rate
    );

    ASSERT_EQ(engine_load_patch(engine(), kPatch), 0);
    ASSERT_ENGINE_START(engine());

    constexpr int NOTE_MS    = 600;  // sustained to hear the tone
    constexpr int RELEASE_MS = 300;  // 150ms release + margin

    const int notes[] = {60, 64, 67, 72};  // C4, E4, G4, C5
    std::cout << "[EnglishHorn] Playing C4 → E4 → G4 → C5…\n";
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
