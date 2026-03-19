/**
 * @file test_delay_lead_patch.cpp
 * @brief Functional tests for delay_lead.json — lead synth with echo delay.
 *
 * Patch topology:
 *   VCO (saw=0.7, pulse=0.5, pw=0.5) → SH_FILTER (cutoff=2400, res=0.25)
 *   → VCA ← ADSR (attack=8ms, decay=50ms, sustain=0.9, release=150ms)
 *   VCA → ECHO_DELAY (time=125ms, feedback=0.45, mix=0.4)
 *
 * Characteristic features:
 *   - Fast attack (8ms) for crisp lead articulation
 *   - High sustain (0.9) keeps the lead singing
 *   - ECHO_DELAY at 125ms creates dotted-eighth delay character
 *   - Feedback=0.45 gives ~3-4 clearly audible echo repeats before decay
 *
 * Tests:
 *   1. NoteOnProducesAudio  — first 5 blocks after note_on(A4), RMS > 0.001.
 *   2. EchoTailPersists     — after 15-block note, delay tail > 0.001 RMS for 15 blocks.
 *   3. AscendingMelodyAudible — C4/E4/G4/A4/C5 ascending line with echo ring.
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

class DelayLeadPatchTest : public ::testing::Test {
protected:
    void SetUp() override {
        test::init_test_environment();
        sample_rate = test::get_safe_sample_rate(0);
        engine_wrapper = std::make_unique<test::EngineWrapper>(sample_rate);
    }

    EngineHandle engine() { return engine_wrapper->get(); }

    int sample_rate;
    std::unique_ptr<test::EngineWrapper> engine_wrapper;

    static constexpr const char* kPatch = "patches/delay_lead.json";
    static constexpr size_t FRAMES = 512;
};

// ---------------------------------------------------------------------------
// Test 1: NoteOnProducesAudio — lead onset is non-silent
// ---------------------------------------------------------------------------

TEST_F(DelayLeadPatchTest, NoteOnProducesAudio) {
    PRINT_TEST_HEADER(
        "Delay Lead — Smoke",
        "Note-on(A4) produces non-silent audio at onset through the lead chain.",
        "engine_load_patch(delay_lead.json) → note_on(A4=69) → engine_process × 5",
        "RMS > 0.001 within first 5 blocks.",
        sample_rate
    );

    ASSERT_EQ(engine_load_patch(engine(), kPatch), 0);
    engine_note_on(engine(), 69, 1.0f);  // A4

    std::vector<float> buf(FRAMES * 2);
    double sum_sq = 0.0;
    for (int b = 0; b < 5; ++b) {
        engine_process(engine(), buf.data(), FRAMES);
        for (size_t i = 0; i < FRAMES; ++i) sum_sq += double(buf[i * 2]) * double(buf[i * 2]);
    }
    float rms = float(std::sqrt(sum_sq / double(FRAMES * 5)));
    std::cout << "[DelayLead] Onset RMS (5 blocks): " << rms << "\n";
    EXPECT_GT(rms, 0.001f) << "Expected non-silent lead output at onset";

    engine_note_off(engine(), 69);
}

// ---------------------------------------------------------------------------
// Test 2: EchoTailPersists — echo delay keeps signal alive after note-off
//
// Protocol:
//   - note_on(A4), run 15 blocks (~160ms, well into sustain)
//   - note_off
//   - run 15 more blocks (~160ms) and measure RMS
//   - ECHO_DELAY time=125ms means first echo arrives at 125ms after onset,
//     feedback=0.45 keeps energy alive for several repeats
// ---------------------------------------------------------------------------

TEST_F(DelayLeadPatchTest, EchoTailPersists) {
    PRINT_TEST_HEADER(
        "Delay Lead — Echo Tail",
        "After note-off, ECHO_DELAY (time=125ms, feedback=0.45) keeps signal alive ~160ms later.",
        "note_on(A4) → 15 blocks → note_off → 15 blocks → assert RMS > 0.001",
        "RMS > 0.001 in the 15 blocks (~160ms) following note-off.",
        sample_rate
    );

    ASSERT_EQ(engine_load_patch(engine(), kPatch), 0);
    engine_note_on(engine(), 69, 1.0f);  // A4

    std::vector<float> buf(FRAMES * 2);

    // Hold for 15 blocks (~160ms, into sustain portion)
    for (int b = 0; b < 15; ++b)
        engine_process(engine(), buf.data(), FRAMES);

    engine_note_off(engine(), 69);

    // Measure 15 blocks after note-off (~160ms)
    double sum_sq = 0.0;
    for (int b = 0; b < 15; ++b) {
        engine_process(engine(), buf.data(), FRAMES);
        for (size_t i = 0; i < FRAMES; ++i) sum_sq += double(buf[i * 2]) * double(buf[i * 2]);
    }
    float rms = float(std::sqrt(sum_sq / double(FRAMES * 15)));
    std::cout << "[DelayLead] Echo tail RMS (~160ms after note-off): " << rms << "\n";
    EXPECT_GT(rms, 0.001f) << "Expected echo delay tail to keep signal alive after note-off";
}

// ---------------------------------------------------------------------------
// Test 3: AscendingMelodyAudible — melodic line with echo ring
// ---------------------------------------------------------------------------

TEST_F(DelayLeadPatchTest, AscendingMelodyAudible) {
    PRINT_TEST_HEADER(
        "Delay Lead — Ascending Melody (audible)",
        "Play C4/E4/G4/A4/C5 ascending line. Notes held 400ms with 600ms release "
        "so echo repeats ring out between each note.",
        "engine_load_patch(delay_lead.json) → engine_start → ascending 5-note line",
        "Audible lead melody with repeating echo tail on each note.",
        sample_rate
    );

    ASSERT_EQ(engine_load_patch(engine(), kPatch), 0);
    ASSERT_ENGINE_START(engine());

    constexpr int NOTE_MS    = 400;
    constexpr int RELEASE_MS = 600;  // let echo ring between notes

    const int notes[] = {60, 64, 67, 69, 72};  // C4, E4, G4, A4, C5
    std::cout << "[DelayLead] Playing ascending C4/E4/G4/A4/C5 with echo...\n";
    for (int n : notes) {
        engine_note_on(engine(), n, 0.8f);
        std::this_thread::sleep_for(std::chrono::milliseconds(NOTE_MS));
        engine_note_off(engine(), n);
        std::this_thread::sleep_for(std::chrono::milliseconds(RELEASE_MS));
    }

    engine_stop(engine());
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
