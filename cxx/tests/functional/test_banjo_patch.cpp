/**
 * @file test_banjo_patch.cpp
 * @brief Functional tests for banjo.json — plucked bright percussive string.
 *
 * Patch topology:
 *   COMPOSITE_GENERATOR (pulse=0.8 + sine=0.4) → SH_FILTER (cutoff=5000 Hz, res=0.45)
 *       → VCA ← AD_ENVELOPE (attack=1ms, decay=0.28s)
 *   AD_ENVELOPE also modulates VCF.cutoff_cv (bright onset, darkening decay).
 *
 * Banjo character from Roland §2-6: the pulse wave (odd harmonics) gives the
 * nasal "twang", the SH_FILTER at high resonance adds the characteristic
 * metallic peak, and the AD envelope creates the percussive pluck.  The
 * dual envelope routing (VCA + VCF) means the onset is maximally bright
 * and the tone darkens as the note decays — matching real string behaviour.
 *
 * Key assertions:
 *   1. Smoke           — note_on produces immediate audio (1ms attack).
 *   2. PercussiveDecay — onset RMS >> tail RMS (AD, no sustain).
 *   3. BrightSpectrum  — spectral centroid at onset > 1500 Hz (resonant SH_FILTER).
 *   4. Audible         — open-G banjo tuning arpeggio (D3/G3/B3/D4/G4).
 *
 * Timing at 48 kHz, block=512 (~10.67ms/block):
 *   Onset: blocks 1–4   (~10–43ms)
 *   Tail:  blocks 25–30 (~267–320ms ≈ 1× decay constant)
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

class BanjoPatchTest : public ::testing::Test {
protected:
    void SetUp() override {
        test::init_test_environment();
        sample_rate = test::get_safe_sample_rate(0);
        engine_wrapper = std::make_unique<test::EngineWrapper>(sample_rate);
    }

    EngineHandle engine() { return engine_wrapper->get(); }

    int sample_rate;
    std::unique_ptr<test::EngineWrapper> engine_wrapper;

    static constexpr const char* kPatch = "patches/banjo.json";
};

// ---------------------------------------------------------------------------
// Test 1: Smoke — immediate audio on note_on (attack=1ms)
// ---------------------------------------------------------------------------

TEST_F(BanjoPatchTest, NoteOnProducesAudio) {
    PRINT_TEST_HEADER(
        "Banjo — Smoke",
        "AD_ENVELOPE attack=1ms: first blocks after note_on should carry signal.",
        "engine_load_patch(banjo.json) → note_on(G3) → 5 blocks",
        "RMS > 0.001",
        sample_rate
    );

    ASSERT_EQ(engine_load_patch(engine(), kPatch), 0);
    engine_note_on(engine(), 55, 1.0f);  // G3

    const size_t FRAMES = 512;
    std::vector<float> buf(FRAMES * 2);
    double sum_sq = 0.0;
    for (int b = 0; b < 5; ++b) {
        engine_process(engine(), buf.data(), FRAMES);
        for (size_t i = 0; i < FRAMES; ++i) sum_sq += double(buf[i * 2]) * double(buf[i * 2]);
    }
    engine_note_off(engine(), 55);

    float rms = float(std::sqrt(sum_sq / double(FRAMES * 5)));
    std::cout << "[Banjo] Onset (5 blocks) RMS: " << rms << "\n";
    EXPECT_GT(rms, 0.001f) << "Expected immediate banjo pluck from 1ms AD attack";
}

// ---------------------------------------------------------------------------
// Test 2: PercussiveDecay — onset RMS >> tail RMS
//
// onset: blocks 1–4  (~10–43ms, near peak)
// tail:  blocks 25–30 (~267–320ms, ≈ 1× decay constant)
// Expected: rms_onset / rms_tail ≥ 3.0
// ---------------------------------------------------------------------------

TEST_F(BanjoPatchTest, PercussiveDecay) {
    PRINT_TEST_HEADER(
        "Banjo — Percussive Decay (automated)",
        "AD_ENVELOPE decay=0.28s: onset RMS (~40ms) ≥ 3× tail RMS (~290ms).",
        "engine_load_patch → note_on(G3) → 30 blocks → compare onset vs tail RMS",
        "rms_onset / rms_tail ≥ 3.0",
        sample_rate
    );

    ASSERT_EQ(engine_load_patch(engine(), kPatch), 0);
    engine_note_on(engine(), 55, 1.0f);  // G3

    const size_t FRAMES      = 512;
    const int    ONSET_START = 1;
    const int    ONSET_END   = 5;   // ~10–53ms
    const int    TAIL_START  = 24;
    const int    TAIL_END    = 30;  // ~256–320ms

    std::vector<float> buf(FRAMES * 2);
    double onset_sq = 0.0; int onset_n = 0;
    double tail_sq  = 0.0; int tail_n  = 0;

    for (int b = 0; b < TAIL_END; ++b) {
        engine_process(engine(), buf.data(), FRAMES);
        double block_sq = 0.0;
        for (size_t i = 0; i < FRAMES; ++i) block_sq += double(buf[i * 2]) * double(buf[i * 2]);
        if (b >= ONSET_START && b < ONSET_END) { onset_sq += block_sq; ++onset_n; }
        if (b >= TAIL_START  && b < TAIL_END)  { tail_sq  += block_sq; ++tail_n;  }
    }
    engine_note_off(engine(), 55);

    float rms_onset = float(std::sqrt(onset_sq / double(FRAMES * onset_n)));
    float rms_tail  = float(std::sqrt(tail_sq  / double(FRAMES * tail_n)));
    float ratio     = rms_tail > 1e-9f ? rms_onset / rms_tail : 0.0f;

    std::cout << "[Banjo] RMS onset (~10–53ms):   " << rms_onset << "\n";
    std::cout << "[Banjo] RMS tail  (~256–320ms): " << rms_tail  << "\n";
    std::cout << "[Banjo] Ratio onset/tail:        " << ratio     << "\n";

    EXPECT_GT(rms_onset, 0.001f) << "No signal at onset — check patch connectivity";
    EXPECT_GT(ratio, 3.0f)
        << "Expected onset/tail ratio ≥ 3 (AD_ENVELOPE, no sustain); got " << ratio;
}

// ---------------------------------------------------------------------------
// Test 3: BrightSpectrum — onset spectral centroid > 1500 Hz
//
// SH_FILTER at cutoff=5000 Hz with resonance=0.45 passes most of the spectrum
// and boosts the region around 5 kHz.  The ENV→VCF.cutoff_cv at near-peak
// envelope pushes the effective cutoff even higher.  The pulse wave
// (odd-harmonic rich) combined with the resonant filter should yield a
// spectral centroid well above 1500 Hz at onset.
// ---------------------------------------------------------------------------

TEST_F(BanjoPatchTest, BrightSpectrum) {
    PRINT_TEST_HEADER(
        "Banjo — Bright Spectrum (automated)",
        "Resonant SH_FILTER at 5kHz + pulse wave: onset spectral centroid > 1500 Hz.",
        "engine_load_patch → note_on(G3) → capture 2048-sample onset window → centroid",
        "centroid > 1500 Hz",
        sample_rate
    );

    ASSERT_EQ(engine_load_patch(engine(), kPatch), 0);
    engine_note_on(engine(), 55, 1.0f);  // G3 = 196 Hz

    const size_t FRAMES = 512;
    const size_t WINDOW = 2048;
    std::vector<float> buf(FRAMES * 2);
    std::vector<float> onset_win;
    onset_win.reserve(WINDOW);

    // Capture starting at block 1 (past 1ms attack transient)
    for (int b = 0; b < 8; ++b) {
        engine_process(engine(), buf.data(), FRAMES);
        if (b >= 1 && onset_win.size() < WINDOW)
            for (size_t i = 0; i < FRAMES && onset_win.size() < WINDOW; ++i)
                onset_win.push_back(buf[i * 2]);
    }
    engine_note_off(engine(), 55);

    ASSERT_EQ(onset_win.size(), WINDOW);

    float centroid = spectral_centroid(onset_win, sample_rate);
    std::cout << "[Banjo] G3 fundamental:    196 Hz\n";
    std::cout << "[Banjo] Spectral centroid: " << centroid << " Hz\n";

    EXPECT_GT(centroid, 1500.0f)
        << "Expected bright banjo centroid > 1500 Hz from resonant SH_FILTER + pulse wave";
}

// ---------------------------------------------------------------------------
// Test 4: Audible — open-G banjo tuning arpeggio
// ---------------------------------------------------------------------------

TEST_F(BanjoPatchTest, OpenGTuningAudible) {
    PRINT_TEST_HEADER(
        "Banjo — Open-G Arpeggio (audible)",
        "Banjo open-G tuning: D3/G3/B3/D4/G4 picked sequence.",
        "engine_load_patch(banjo.json) → engine_start → D3/G3/B3/D4/G4",
        "Audible twangy banjo pluck with bright resonant SH_FILTER character.",
        sample_rate
    );

    ASSERT_EQ(engine_load_patch(engine(), kPatch), 0);
    ASSERT_ENGINE_START(engine());

    constexpr int NOTE_MS    = 25;   // very short gate (pick release)
    constexpr int RELEASE_MS = 450;  // let the 0.28s decay ring out

    // Open-G banjo strings: D3=50, G3=55, B3=59, D4=62, G4=67
    const int notes[] = {50, 55, 59, 62, 67};
    std::cout << "[Banjo] Playing D3 → G3 → B3 → D4 → G4 (open-G)…\n";
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
