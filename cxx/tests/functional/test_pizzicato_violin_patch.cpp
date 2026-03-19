/**
 * @file test_pizzicato_violin_patch.cpp
 * @brief Functional tests for pizzicato_violin.json — plucked string via AD envelope.
 *
 * Patch topology:
 *   COMPOSITE_GENERATOR (saw+sine) → SH_FILTER (cutoff=2800 Hz)
 *       → VCA ← AD_ENVELOPE (attack=1ms, decay=0.22s)
 *   AD_ENVELOPE also modulates VCF.cutoff_cv (classic "bright pluck" trick).
 *
 * Roland §4-6 describes the plucked-string shape as a fast attack followed by
 * a natural exponential decay with sustain=0 — exactly what AD_ENVELOPE provides.
 * The dual-routing of ENV → VCF.cutoff_cv + VCA.gain_cv means the tone starts
 * bright and darkens as it decays, matching the natural string overtone rolloff.
 *
 * Key assertions:
 *   1. Smoke          — note_on produces audio immediately (1ms attack).
 *   2. PercussiveDecay — onset RMS >> tail RMS (decay=0.22s, no sustain).
 *   3. BrightOnset     — spectral centroid at onset is higher than in the tail.
 *   4. Audible         — pizzicato figure on C4/F4/A4/C5.
 *
 * Timing at 48 kHz, block=512 (~10.67ms/block):
 *   Onset: blocks 1–4   (~10–43ms  from note_on, near-peak)
 *   Tail:  blocks 21–26 (~224–277ms, ≈ 1× decay constant)
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

class PizzicatoViolinPatchTest : public ::testing::Test {
protected:
    void SetUp() override {
        test::init_test_environment();
        sample_rate = test::get_safe_sample_rate(0);
        engine_wrapper = std::make_unique<test::EngineWrapper>(sample_rate);
    }

    EngineHandle engine() { return engine_wrapper->get(); }

    int sample_rate;
    std::unique_ptr<test::EngineWrapper> engine_wrapper;

    static constexpr const char* kPatch = "patches/pizzicato_violin.json";
};

// ---------------------------------------------------------------------------
// Test 1: Smoke — immediate audio on note_on (attack=1ms)
// ---------------------------------------------------------------------------

TEST_F(PizzicatoViolinPatchTest, NoteOnProducesAudio) {
    PRINT_TEST_HEADER(
        "Pizzicato Violin — Smoke",
        "1ms attack AD envelope: first block after note_on should have audible signal.",
        "engine_load_patch(pizzicato_violin.json) → note_on(A4) → 4 blocks",
        "RMS > 0.001",
        sample_rate
    );

    ASSERT_EQ(engine_load_patch(engine(), kPatch), 0);
    engine_note_on(engine(), 69, 1.0f);  // A4

    const size_t FRAMES = 512;
    std::vector<float> buf(FRAMES * 2);
    double sum_sq = 0.0;
    for (int b = 0; b < 4; ++b) {
        engine_process(engine(), buf.data(), FRAMES);
        for (size_t i = 0; i < FRAMES; ++i) sum_sq += double(buf[i * 2]) * double(buf[i * 2]);
    }
    engine_note_off(engine(), 69);

    float rms = float(std::sqrt(sum_sq / double(FRAMES * 4)));
    std::cout << "[Pizzicato] Onset (4 blocks) RMS: " << rms << "\n";
    EXPECT_GT(rms, 0.001f) << "Expected immediate audio from 1ms AD attack";
}

// ---------------------------------------------------------------------------
// Test 2: PercussiveDecay — onset RMS >> tail RMS
//
// onset: blocks 1–4  (~10–43ms):  near peak amplitude
// tail:  blocks 21–26 (~224–277ms): ~1× decay constant from note_on
// Expected: rms_onset / rms_tail ≥ 3.0
// ---------------------------------------------------------------------------

TEST_F(PizzicatoViolinPatchTest, PercussiveDecay) {
    PRINT_TEST_HEADER(
        "Pizzicato Violin — Percussive Decay (automated)",
        "AD_ENVELOPE decay=0.22s: onset RMS (~40ms) ≥ 3× tail RMS (~250ms).",
        "engine_load_patch → note_on(A4) → 26 blocks → compare onset vs tail RMS",
        "rms_onset / rms_tail ≥ 3.0",
        sample_rate
    );

    ASSERT_EQ(engine_load_patch(engine(), kPatch), 0);
    engine_note_on(engine(), 69, 1.0f);  // A4

    const size_t FRAMES      = 512;
    const int    ONSET_START = 1;
    const int    ONSET_END   = 5;   // ~10–53ms
    const int    TAIL_START  = 20;
    const int    TAIL_END    = 26;  // ~213–277ms

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
    engine_note_off(engine(), 69);

    float rms_onset = float(std::sqrt(onset_sq / double(FRAMES * onset_n)));
    float rms_tail  = float(std::sqrt(tail_sq  / double(FRAMES * tail_n)));
    float ratio     = rms_tail > 1e-9f ? rms_onset / rms_tail : 0.0f;

    std::cout << "[Pizzicato] RMS onset (~10–53ms):   " << rms_onset << "\n";
    std::cout << "[Pizzicato] RMS tail  (~213–277ms): " << rms_tail  << "\n";
    std::cout << "[Pizzicato] Ratio onset/tail:        " << ratio     << "\n";

    EXPECT_GT(rms_onset, 0.001f) << "No signal at onset — check patch connectivity";
    EXPECT_GT(ratio, 3.0f)
        << "Expected onset/tail ratio ≥ 3 (AD decay=0.22s); got " << ratio;
}

// ---------------------------------------------------------------------------
// Test 3: BrightOnset — spectral centroid drops as envelope decays
//
// The dual ENV → VCF.cutoff_cv + VCA.gain_cv connection means higher envelope
// value → higher cutoff → brighter tone.  As the envelope decays, the filter
// closes and the centroid falls.
//
// onset window (blocks 1–4): high ENV → filter open → bright
// tail  window (blocks 20–24): low ENV → filter closing → darker
// Expected: centroid_onset > centroid_tail × 1.2
// ---------------------------------------------------------------------------

TEST_F(PizzicatoViolinPatchTest, BrightOnsetDarkTail) {
    PRINT_TEST_HEADER(
        "Pizzicato Violin — Bright Onset / Dark Tail (automated)",
        "ENV → VCF.cutoff_cv causes onset to be brighter than tail as envelope decays.",
        "engine_load_patch → note_on(A4) → capture onset window, tail window → centroids",
        "centroid_onset > centroid_tail × 1.2",
        sample_rate
    );

    ASSERT_EQ(engine_load_patch(engine(), kPatch), 0);
    engine_note_on(engine(), 69, 1.0f);  // A4

    const size_t FRAMES = 512;
    const size_t WINDOW = 1024;

    std::vector<float> buf(FRAMES * 2);
    std::vector<float> onset_win, tail_win;
    onset_win.reserve(WINDOW);
    tail_win.reserve(WINDOW);

    for (int b = 0; b < 26; ++b) {
        engine_process(engine(), buf.data(), FRAMES);
        if (b >= 1 && b < 6  && onset_win.size() < WINDOW)
            for (size_t i = 0; i < FRAMES && onset_win.size() < WINDOW; ++i)
                onset_win.push_back(buf[i * 2]);
        if (b >= 20 && b < 25 && tail_win.size() < WINDOW)
            for (size_t i = 0; i < FRAMES && tail_win.size() < WINDOW; ++i)
                tail_win.push_back(buf[i * 2]);
    }
    engine_note_off(engine(), 69);

    ASSERT_EQ(onset_win.size(), WINDOW);
    ASSERT_EQ(tail_win.size(),  WINDOW);

    float centroid_onset = spectral_centroid(onset_win, sample_rate);
    float centroid_tail  = spectral_centroid(tail_win,  sample_rate);

    std::cout << "[Pizzicato] Centroid onset (~10–53ms):   " << centroid_onset << " Hz\n";
    std::cout << "[Pizzicato] Centroid tail  (~213–267ms): " << centroid_tail  << " Hz\n";

    EXPECT_GT(centroid_onset, centroid_tail * 1.2f)
        << "Expected onset centroid > 1.2× tail centroid (dual ENV→VCF brightens pluck onset)";
}

// ---------------------------------------------------------------------------
// Test 4: Audible — pizzicato figure C4 / F4 / A4 / C5
// ---------------------------------------------------------------------------

TEST_F(PizzicatoViolinPatchTest, PizzicatoFigureAudible) {
    PRINT_TEST_HEADER(
        "Pizzicato Violin — C4/F4/A4/C5 Figure (audible)",
        "Short plucked notes on a C major arpeggio to hear the bright onset and fast decay.",
        "engine_load_patch(pizzicato_violin.json) → engine_start → C4/F4/A4/C5",
        "Audible percussive violin plucks with bright attack and natural decay.",
        sample_rate
    );

    ASSERT_EQ(engine_load_patch(engine(), kPatch), 0);
    ASSERT_ENGINE_START(engine());

    constexpr int NOTE_MS    = 30;   // short gate — pluck
    constexpr int RELEASE_MS = 400;  // let the 0.22s decay ring out

    const int notes[] = {60, 65, 69, 72};  // C4, F4, A4, C5
    std::cout << "[Pizzicato] Playing C4 → F4 → A4 → C5…\n";
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
