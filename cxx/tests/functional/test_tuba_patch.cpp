/**
 * @file test_tuba_patch.cpp
 * @brief Functional tests for tuba.json (Tuba variant of Fig 1-6).
 *
 * Patch topology:
 *   VCO (saw, 32') → VCF → VCA. ADSR → CV_SPLITTER → VCA gain_cv +
 *   (via CV_SCALER scale=0.5) VCF cutoff_cv.
 *
 * footage=32 shifts pitch down 2 octaves (-24 semitones) for the sub-bass tuba register.
 * MIDI C4 sounds as C2. Lower VCF cutoff (1500Hz) for dark tubby character.
 * Shorter ADSR: attack=20ms, decay=20ms, sustain=0.60, release=20ms.
 *
 * Tests:
 *   1. NoteOnProducesAudio — smoke: patch loads and note-on produces non-silent audio.
 *                            footage=32 transposes VCO down 2 octaves — MIDI C4 sounds as C2.
 *   2. TubaScaleAudible    — C4/E4/G4/C5 (sounds as C2/E2/G2/C3 due to footage=32).
 *   3. TubaMidiAudible     — play brass.mid through tuba patch for live listening.
 *                            footage=32 transposes brass.mid down 2 octaves to tuba register.
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

class TubaPatchTest : public ::testing::Test {
protected:
    void SetUp() override {
        test::init_test_environment();
        sample_rate = test::get_safe_sample_rate(0);
        engine_wrapper = std::make_unique<test::EngineWrapper>(sample_rate);
    }

    EngineHandle engine() { return engine_wrapper->get(); }

    int sample_rate;
    std::unique_ptr<test::EngineWrapper> engine_wrapper;

    static constexpr const char* kPatch = "patches/tuba.json";
    static constexpr const char* kMidi  = "midi/brass.mid";
};

// ---------------------------------------------------------------------------
// Test 1: Smoke — patch loads and produces audio
//
// Note: footage=32 transposes VCO down 2 octaves — MIDI C4 sounds as C2.
// ---------------------------------------------------------------------------

TEST_F(TubaPatchTest, NoteOnProducesAudio) {
    PRINT_TEST_HEADER(
        "Tuba — Smoke",
        "Note-on produces non-silent audio through VCO (saw, 32') → VCF → VCA chain. "
        "footage=32 transposes VCO down 2 octaves — MIDI C4 sounds as C2.",
        "engine_load_patch(tuba.json) → note_on → engine_process × 8",
        "RMS > 0.001 across 5 measured blocks (skip 3, measure 5).",
        sample_rate
    );

    ASSERT_EQ(engine_load_patch(engine(), kPatch), 0);
    engine_note_on(engine(), 60, 1.0f);  // C4 (sounds as C2 with footage=32)

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
    std::cout << "[Tuba] 5-block RMS (skip 3): " << rms << "\n";
    EXPECT_GT(rms, 0.001f) << "Expected non-silent output at onset";

    engine_note_off(engine(), 60);
}

// ---------------------------------------------------------------------------
// Test 2: Audible — tuba scale
//
// MIDI notes C4/E4/G4/C5 (60/64/67/72) sound as C2/E2/G2/C3 due to footage=32.
// ---------------------------------------------------------------------------

TEST_F(TubaPatchTest, TubaScaleAudible) {
    PRINT_TEST_HEADER(
        "Tuba — Scale (audible)",
        "Play C4/E4/G4/C5 MIDI notes — these sound as C2/E2/G2/C3 due to footage=32. "
        "Hear the dark, sub-bass tubby character with low cutoff (1500Hz).",
        "engine_load_patch(tuba.json) → engine_start → C4/E4/G4/C5 (→ C2/E2/G2/C3)",
        "Audible deep tuba tone in sub-bass register.",
        sample_rate
    );

    ASSERT_EQ(engine_load_patch(engine(), kPatch), 0);
    ASSERT_ENGINE_START(engine());

    constexpr int NOTE_MS    = 800;
    constexpr int RELEASE_MS = 300;

    // MIDI pitches: sound 2 octaves lower due to footage=32
    const int notes[] = {60, 64, 67, 72};  // C4→C2, E4→E2, G4→G2, C5→C3
    std::cout << "[Tuba] Playing C4/E4/G4/C5 (sounds as C2/E2/G2/C3 with footage=32)…\n";
    for (int midi : notes) {
        engine_note_on(engine(), midi, 1.0f);
        std::this_thread::sleep_for(std::chrono::milliseconds(NOTE_MS));
        engine_note_off(engine(), midi);
        std::this_thread::sleep_for(std::chrono::milliseconds(RELEASE_MS));
    }

    engine_stop(engine());
}

// ---------------------------------------------------------------------------
// Test 3: MIDI — play brass.mid through tuba patch
//
// footage=32 transposes brass.mid down 2 octaves to tuba register.
// ---------------------------------------------------------------------------

TEST_F(TubaPatchTest, TubaMidiAudible) {
    PRINT_TEST_HEADER(
        "Tuba — MIDI playback (audible)",
        "Play brass.mid through tuba.json for live listening. "
        "footage=32 transposes brass.mid down 2 octaves to tuba register.",
        "engine_load_patch(tuba.json) → engine_start → engine_load_midi → engine_midi_play",
        "Audible tuba tone in sub-bass register (~15s).",
        sample_rate
    );

    if (std::ifstream f(kMidi); !f.good()) {
        GTEST_SKIP() << kMidi << " not found";
    }

    ASSERT_EQ(engine_load_patch(engine(), kPatch), 0);
    ASSERT_ENGINE_START(engine());
    ASSERT_EQ(engine_load_midi(engine(), kMidi), 0);
    engine_midi_play(engine());

    std::cout << "[Tuba] Playing brass.mid through Tuba patch…\n";
    test::wait_while_running(15);

    engine_midi_stop(engine());
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
