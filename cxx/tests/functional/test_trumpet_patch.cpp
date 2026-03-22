/**
 * @file test_trumpet_patch.cpp
 * @brief Functional tests for trumpet.json (Trumpet variant of Fig 1-6).
 *
 * Patch topology:
 *   Trumpet variant of Fig 1-6: pulse waveform, VCF MOD scale=0.9.
 *   VCO (pulse 50%, 8') → VCF → VCA. ADSR → CV_SPLITTER → VCA gain_cv +
 *   (via CV_SCALER scale=0.9) VCF cutoff_cv.
 *
 * Pulse waveform adds nasal brightness vs Horn sawtooth. Stronger filter sweep
 * (scale=0.9 vs Horn's 0.5) gives a sharper attack transient.
 *
 * ADSR: attack=10ms, decay=80ms, sustain=0.65, release=150ms.
 *
 * Tests:
 *   1. NoteOnProducesAudio — smoke: patch loads and note-on produces non-silent audio.
 *   2. DecayToSustain      — RMS near peak (21–53ms) is ≥1.15× settled sustain RMS.
 *   3. CMajorArpeggioAudible — C4–E4–G4–C5 ascending arpeggio.
 *   4. TrumpetMidiAudible  — play brass.mid through trumpet patch for live listening.
 */

#include <gtest/gtest.h>
#include "../TestHelper.hpp"
#include <vector>
#include <cmath>
#include <thread>
#include <chrono>
#include <fstream>

// ---------------------------------------------------------------------------
// Fixture
// ---------------------------------------------------------------------------

class TrumpetPatchTest : public ::testing::Test {
protected:
    void SetUp() override {
        test::init_test_environment();
        sample_rate = test::get_safe_sample_rate(0);
        engine_wrapper = std::make_unique<test::EngineWrapper>(sample_rate);
    }

    EngineHandle engine() { return engine_wrapper->get(); }

    int sample_rate;
    std::unique_ptr<test::EngineWrapper> engine_wrapper;

    static constexpr const char* kPatch = "patches/trumpet.json";
    static constexpr const char* kMidi  = "midi/brass.mid";
};

// ---------------------------------------------------------------------------
// Test 1: Smoke — patch loads and produces audio
// ---------------------------------------------------------------------------

TEST_F(TrumpetPatchTest, NoteOnProducesAudio) {
    PRINT_TEST_HEADER(
        "Trumpet — Smoke",
        "Note-on produces non-silent audio through VCO (pulse) → VCF → VCA chain.",
        "engine_load_patch(trumpet.json) → note_on → engine_process × 8",
        "RMS > 0.001 across 5 measured blocks (skip 3, measure 5).",
        sample_rate
    );

    ASSERT_EQ(engine_load_patch(engine(), kPatch), 0);
    engine_note_on(engine(), 60, 1.0f);  // C4

    const size_t FRAMES = 512;
    std::vector<float> buf(FRAMES * 2);
    double sum_sq = 0.0;
    for (int b = 0; b < 8; ++b) {
        engine_process(engine(), buf.data(), FRAMES);
        if (b >= 3) {
            for (size_t i = 0; i < FRAMES; ++i)
                sum_sq += double(buf[i * 2]) * double(buf[i * 2]);
        }
    }
    float rms = float(std::sqrt(sum_sq / double(FRAMES * 5)));
    std::cout << "[Trumpet] 5-block RMS (skip 3): " << rms << "\n";
    EXPECT_GT(rms, 0.001f) << "Expected non-silent output at onset";

    engine_note_off(engine(), 60);
}

// ---------------------------------------------------------------------------
// Test 2: DecayToSustain — initial peak louder than settled sustain
//
// ADSR: attack=10ms → peak near blocks 2–5 (~21–53ms from note-on).
// Decay: 80ms time constant → settled at sustain=0.65 by ~300ms.
// SUSTAIN window: blocks 30–40 (~320–427ms).
//
// RMS ∝ amplitude, and sustain=0.65 means sustain ≈ 65% of peak.
// Expected ratio: rms_peak / rms_sustain ≥ 1.15 (conservative).
// ---------------------------------------------------------------------------

TEST_F(TrumpetPatchTest, DecayToSustain) {
    PRINT_TEST_HEADER(
        "Trumpet — Decay to Sustain (automated)",
        "RMS near peak (21–53ms) is ≥1.15× the settled sustain RMS (~320–427ms), "
        "confirming the ADSR decay stage drops from 1.0 to sustain=0.65.",
        "engine_load_patch → note_on(C4) → engine_process → compare peak vs sustain RMS",
        "rms_peak / rms_sustain ≥ 1.15",
        sample_rate
    );

    ASSERT_EQ(engine_load_patch(engine(), kPatch), 0);
    engine_note_on(engine(), 60, 1.0f);  // C4

    const size_t FRAMES        = 512;
    const int    PEAK_START    = 2;    // ~21ms — past 10ms attack, near peak
    const int    PEAK_END      = 6;    // ~64ms
    const int    SUSTAIN_START = 30;   // ~320ms — decay fully settled
    const int    SUSTAIN_END   = 40;   // ~427ms

    std::vector<float> buf(FRAMES * 2);
    double peak_sq = 0.0;    int peak_n    = 0;
    double sustain_sq = 0.0; int sustain_n = 0;

    for (int b = 0; b < SUSTAIN_END; ++b) {
        engine_process(engine(), buf.data(), FRAMES);
        double block_sq = 0.0;
        for (size_t i = 0; i < FRAMES; ++i) block_sq += double(buf[i * 2]) * double(buf[i * 2]);
        if (b >= PEAK_START    && b < PEAK_END)    { peak_sq    += block_sq; ++peak_n;    }
        if (b >= SUSTAIN_START && b < SUSTAIN_END) { sustain_sq += block_sq; ++sustain_n; }
    }
    engine_note_off(engine(), 60);

    float rms_peak    = float(std::sqrt(peak_sq    / double(FRAMES * peak_n)));
    float rms_sustain = float(std::sqrt(sustain_sq / double(FRAMES * sustain_n)));
    float ratio       = rms_sustain > 1e-6f ? rms_peak / rms_sustain : 0.0f;

    std::cout << "[Trumpet] RMS peak    (~21–53ms): " << rms_peak    << "\n";
    std::cout << "[Trumpet] RMS sustain (~320ms+):  " << rms_sustain << "\n";
    std::cout << "[Trumpet] Ratio peak/sustain:     " << ratio       << "\n";

    EXPECT_GT(rms_sustain, 0.001f) << "No signal during sustain window — envelope may have expired";
    EXPECT_GT(ratio, 1.15f)
        << "Expected peak/sustain ratio ≥ 1.15 (sustain=0.65 → ratio≈1.54); got " << ratio;
}

// ---------------------------------------------------------------------------
// Test 3: Audible — C major arpeggio
// ---------------------------------------------------------------------------

TEST_F(TrumpetPatchTest, CMajorArpeggioAudible) {
    PRINT_TEST_HEADER(
        "Trumpet — C Major Arpeggio (audible)",
        "Play C4–E4–G4–C5 as a rising arpeggio to hear the pulse waveform brightness "
        "and stronger filter sweep (scale=0.9).",
        "engine_load_patch(trumpet.json) → engine_start → C4 / E4 / G4 / C5",
        "Audible trumpet tone with nasal pulse character and filter envelope sweep.",
        sample_rate
    );

    ASSERT_EQ(engine_load_patch(engine(), kPatch), 0);
    ASSERT_ENGINE_START(engine());

    constexpr int NOTE_MS    = 500;
    constexpr int RELEASE_MS = 200;

    const int notes[] = {60, 64, 67, 72};  // C4, E4, G4, C5
    std::cout << "[Trumpet] Playing C4 – E4 – G4 – C5 arpeggio…\n";
    for (int midi : notes) {
        engine_note_on(engine(), midi, 1.0f);
        std::this_thread::sleep_for(std::chrono::milliseconds(NOTE_MS));
        engine_note_off(engine(), midi);
        std::this_thread::sleep_for(std::chrono::milliseconds(RELEASE_MS));
    }

    engine_stop(engine());
}

// ---------------------------------------------------------------------------
// Test 4: MIDI — play brass.mid through trumpet patch
// ---------------------------------------------------------------------------

TEST_F(TrumpetPatchTest, TrumpetMidiAudible) {
    PRINT_TEST_HEADER(
        "Trumpet — MIDI playback (audible)",
        "Play brass.mid through trumpet.json for live listening.",
        "engine_load_patch(trumpet.json) → engine_start → engine_load_midi → engine_midi_play",
        "Audible trumpet tone with filter envelope sweep (~15s).",
        sample_rate
    );

    if (std::ifstream f(kMidi); !f.good()) {
        GTEST_SKIP() << kMidi << " not found";
    }

    ASSERT_EQ(engine_load_patch(engine(), kPatch), 0);
    ASSERT_ENGINE_START(engine());
    ASSERT_EQ(engine_load_midi(engine(), kMidi), 0);
    engine_midi_play(engine());

    std::cout << "[Trumpet] Playing brass.mid through Trumpet patch…\n";
    test::wait_while_running(15);

    engine_midi_stop(engine());
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
