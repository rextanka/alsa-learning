/**
 * @file test_glockenspiel_patch.cpp
 * @brief Functional tests for glockenspiel.json — metallic bar percussion.
 *
 * Patch topology (Roland System 100M Fig. 2-16):
 *   VCO1 (sine, 2') × VCO2 (sine, 2', transpose=+3 semitones)
 *       → RING_MOD → VCA ← ADSR (A=1ms, D=0.4s, S=0, R=0.4s)
 *
 * The ring modulator produces sum and difference sidebands between the two
 * partials.  With VCO2 tuned a minor-3rd above VCO1 the sidebands are
 * inharmonic, giving the metallic tinkle of a struck metal bar.
 * Both VCOs at 2' footage (two octaves above concert pitch — glockenspiel range).
 * Pure ring-mod output to VCA — no direct VCO path (that is an optional experiment
 * described in the Roland book text but not shown in Fig. 2-16).
 *
 * Key assertions:
 *   1. Smoke             — note_on produces immediate audio (1ms attack).
 *   2. InharmonicContent — spectral centroid at onset > fundamental × 1.3
 *                          (ring mod has pushed energy above the fundamental).
 *   3. PercussiveDecay   — onset RMS >> tail RMS (ADSR sustain=0).
 *   4. Audible           — G major pentatonic arpeggio (G4/A4/B4/D5/E5).
 *
 * Timing at 48 kHz, block=512 (~10.67ms/block):
 *   decay=0.4s ≈ 37.5 blocks per time constant
 *   Onset: blocks 1–4   (~10–43ms, near peak)
 *   Tail:  blocks 45–55 (~480–587ms ≈ 1.2× decay constant, env ≈ 0.30)
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

class GlockenspielPatchTest : public ::testing::Test {
protected:
    void SetUp() override {
        test::init_test_environment();
        sample_rate = test::get_safe_sample_rate(0);
        engine_wrapper = std::make_unique<test::EngineWrapper>(sample_rate);
    }

    EngineHandle engine() { return engine_wrapper->get(); }

    int sample_rate;
    std::unique_ptr<test::EngineWrapper> engine_wrapper;

    static constexpr const char* kPatch = "patches/glockenspiel.json";
};

// ---------------------------------------------------------------------------
// Test 1: Smoke — immediate audio on note_on (attack=1ms)
// ---------------------------------------------------------------------------

TEST_F(GlockenspielPatchTest, NoteOnProducesAudio) {
    PRINT_TEST_HEADER(
        "Glockenspiel — Smoke",
        "ADSR_ENVELOPE attack=1ms: first blocks after note_on should carry signal.",
        "engine_load_patch(glockenspiel.json) → note_on(C5) → 5 blocks",
        "RMS > 0.001",
        sample_rate
    );

    ASSERT_EQ(engine_load_patch(engine(), kPatch), 0);
    engine_note_on(engine(), 72, 1.0f);  // C5

    const size_t FRAMES = 512;
    std::vector<float> buf(FRAMES * 2);
    double sum_sq = 0.0;
    for (int b = 0; b < 5; ++b) {
        engine_process(engine(), buf.data(), FRAMES);
        for (size_t i = 0; i < FRAMES; ++i) sum_sq += double(buf[i * 2]) * double(buf[i * 2]);
    }
    engine_note_off(engine(), 72);

    float rms = float(std::sqrt(sum_sq / double(FRAMES * 5)));
    std::cout << "[Glockenspiel] Onset (5 blocks) RMS: " << rms << "\n";
    EXPECT_GT(rms, 0.001f) << "Expected immediate glockenspiel strike from 1ms AD attack";
}

// ---------------------------------------------------------------------------
// Test 2: InharmonicContent — ring mod shifts energy above the fundamental
//
// For C5 (523.25 Hz) with VCO2 a minor 3rd above (Eb5 = 622.25 Hz):
//   sum  sideband: C5 + Eb5 ≈ 1146 Hz
//   diff sideband: Eb5 - C5 ≈ 99 Hz
// The spectral centroid from the mix of sidebands should be well above
// C5 fundamental × 1.3 (≈ 680 Hz), demonstrating inharmonic bar timbre.
// ---------------------------------------------------------------------------

TEST_F(GlockenspielPatchTest, InharmonicContent) {
    PRINT_TEST_HEADER(
        "Glockenspiel — Inharmonic Content (automated)",
        "RING_MOD of two sines: centroid at onset > fundamental × 1.3.",
        "engine_load_patch → note_on(C5) → capture 2048-sample onset window → centroid",
        "centroid > 523.25 × 1.3 ≈ 680 Hz",
        sample_rate
    );

    ASSERT_EQ(engine_load_patch(engine(), kPatch), 0);
    engine_note_on(engine(), 72, 1.0f);  // C5 = 523.25 Hz

    const size_t FRAMES = 512;
    const size_t WINDOW = 2048;
    std::vector<float> buf(FRAMES * 2);
    std::vector<float> onset_win;
    onset_win.reserve(WINDOW);

    for (int b = 0; b < 8; ++b) {
        engine_process(engine(), buf.data(), FRAMES);
        if (b >= 1 && onset_win.size() < WINDOW)
            for (size_t i = 0; i < FRAMES && onset_win.size() < WINDOW; ++i)
                onset_win.push_back(buf[i * 2]);
    }
    engine_note_off(engine(), 72);

    ASSERT_EQ(onset_win.size(), WINDOW);

    float centroid  = spectral_centroid(onset_win, sample_rate);
    float fund_hz   = 523.25f;  // C5
    std::cout << "[Glockenspiel] Fundamental (C5):   " << fund_hz   << " Hz\n";
    std::cout << "[Glockenspiel] Spectral centroid:  " << centroid  << " Hz\n";
    std::cout << "[Glockenspiel] Centroid / fund:    " << centroid / fund_hz << "\n";

    EXPECT_GT(centroid, fund_hz * 1.3f)
        << "Expected ring-mod sidebands to push centroid above 1.3× fundamental";
}

// ---------------------------------------------------------------------------
// Test 3: PercussiveDecay — onset RMS >> tail RMS
//
// onset: blocks 1–4  (~10–43ms, near peak)
// tail:  blocks 45–55 (~480–587ms ≈ 1.2× decay constant, env ≈ 0.30)
// Expected: rms_onset / rms_tail ≥ 3.0
// ---------------------------------------------------------------------------

TEST_F(GlockenspielPatchTest, PercussiveDecay) {
    PRINT_TEST_HEADER(
        "Glockenspiel — Percussive Decay (automated)",
        "ADSR_ENVELOPE decay=0.4s, sustain=0: onset RMS (~40ms) ≥ 3× tail RMS (~530ms).",
        "engine_load_patch → note_on(C5) → 55 blocks → compare onset vs tail RMS",
        "rms_onset / rms_tail ≥ 3.0",
        sample_rate
    );

    ASSERT_EQ(engine_load_patch(engine(), kPatch), 0);
    engine_note_on(engine(), 72, 1.0f);  // C5

    const size_t FRAMES      = 512;
    const int    ONSET_START = 1;
    const int    ONSET_END   = 5;   // ~10–53ms
    const int    TAIL_START  = 45;
    const int    TAIL_END    = 55;  // ~480–587ms, ≈1.2 decay time constants

    std::vector<float> buf(FRAMES * 2);
    double onset_sq = 0.0; int onset_n = 0;
    double tail_sq  = 0.0; int tail_n  = 0;

    for (int b = 0; b < TAIL_END + 1; ++b) {
        engine_process(engine(), buf.data(), FRAMES);
        double block_sq = 0.0;
        for (size_t i = 0; i < FRAMES; ++i) block_sq += double(buf[i * 2]) * double(buf[i * 2]);
        if (b >= ONSET_START && b < ONSET_END) { onset_sq += block_sq; ++onset_n; }
        if (b >= TAIL_START  && b < TAIL_END)  { tail_sq  += block_sq; ++tail_n;  }
    }
    engine_note_off(engine(), 72);

    float rms_onset = float(std::sqrt(onset_sq / double(FRAMES * onset_n)));
    float rms_tail  = float(std::sqrt(tail_sq  / double(FRAMES * tail_n)));
    float ratio     = rms_tail > 1e-9f ? rms_onset / rms_tail : 0.0f;

    std::cout << "[Glockenspiel] RMS onset (~10–53ms):   " << rms_onset << "\n";
    std::cout << "[Glockenspiel] RMS tail  (~480–587ms): " << rms_tail  << "\n";
    std::cout << "[Glockenspiel] Ratio onset/tail:        " << ratio     << "\n";

    EXPECT_GT(rms_onset, 0.001f) << "No signal at onset — check patch connectivity";
    EXPECT_GT(ratio, 3.0f)
        << "Expected onset/tail ratio ≥ 3 (ADSR decay=0.4s, sustain=0); got " << ratio;
}

// ---------------------------------------------------------------------------
// Test 4: Audible — G major pentatonic arpeggio
// ---------------------------------------------------------------------------

TEST_F(GlockenspielPatchTest, PentatonicArpeggioAudible) {
    PRINT_TEST_HEADER(
        "Glockenspiel — G Major Pentatonic (audible)",
        "Bright metallic bar strikes across G4/A4/B4/D5/E5.",
        "engine_load_patch(glockenspiel.json) → engine_start → G4/A4/B4/D5/E5",
        "Audible glassy inharmonic bell tones with ~1s ring.",
        sample_rate
    );

    ASSERT_EQ(engine_load_patch(engine(), kPatch), 0);
    ASSERT_ENGINE_START(engine());

    constexpr int NOTE_MS    = 40;   // short strike
    constexpr int RELEASE_MS = 1200;  // let the 1.0s decay ring out

    // G major pentatonic: G4=67, A4=69, B4=71, D5=74, E5=76
    const int notes[] = {67, 69, 71, 74, 76};
    std::cout << "[Glockenspiel] Playing G4 → A4 → B4 → D5 → E5 (G pentatonic)…\n";
    for (int midi : notes) {
        engine_note_on(engine(), midi, 1.0f);
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
