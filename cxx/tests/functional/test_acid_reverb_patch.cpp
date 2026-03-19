/**
 * @file test_acid_reverb_patch.cpp
 * @brief Functional tests for acid_reverb.json — TB-style acid bass with FDN reverb.
 *
 * Patch topology:
 *   VCO (saw=1.0) → DIODE_FILTER (cutoff=800, res=0.88)
 *   → VCA ← AD_ENVELOPE (attack=3ms, decay=220ms)
 *   AD_ENVELOPE also → VCF.cutoff_cv (1 octave sweep: 800→1600 Hz)
 *   VCA → REVERB_FDN (decay=1.2s, wet=0.20)
 *
 * Characteristic features:
 *   - AD_ENVELOPE (truer to TB-303): percussive, no sustain or release stage
 *   - DIODE_FILTER at high resonance (0.88) gives the rubbery TB-style zap
 *   - Envelope sweeps both VCA gain and filter cutoff for the classic acid wah
 *   - FDN reverb at lower wet (0.20) preserves dry acid character
 *
 * Tests:
 *   1. NoteOnProducesAudio  — first 5 blocks after note_on(C2), RMS > 0.001.
 *   2. ReverbTailPersists   — after AD decay completes, reverb tail > 0.0005 RMS.
 *   3. FmAcidRiffAudible    — 4-bar Fm acid riff at 120 BPM, 16th-note grid.
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

class AcidReverbPatchTest : public ::testing::Test {
protected:
    void SetUp() override {
        test::init_test_environment();
        sample_rate = test::get_safe_sample_rate(0);
        engine_wrapper = std::make_unique<test::EngineWrapper>(sample_rate);
    }

    EngineHandle engine() { return engine_wrapper->get(); }

    int sample_rate;
    std::unique_ptr<test::EngineWrapper> engine_wrapper;

    static constexpr const char* kPatch = "patches/acid_reverb.json";
    static constexpr size_t FRAMES = 512;
};

// ---------------------------------------------------------------------------
// Test 1: NoteOnProducesAudio — acid pluck onset is non-silent
// ---------------------------------------------------------------------------

TEST_F(AcidReverbPatchTest, NoteOnProducesAudio) {
    PRINT_TEST_HEADER(
        "Acid Reverb — Smoke",
        "Note-on(C2) produces non-silent audio at onset through the acid chain.",
        "engine_load_patch(acid_reverb.json) → note_on(C2=36) → engine_process × 5",
        "RMS > 0.001 within first 5 blocks.",
        sample_rate
    );

    ASSERT_EQ(engine_load_patch(engine(), kPatch), 0);
    engine_note_on(engine(), 36, 1.0f);  // C2

    std::vector<float> buf(FRAMES * 2);
    double sum_sq = 0.0;
    for (int b = 0; b < 5; ++b) {
        engine_process(engine(), buf.data(), FRAMES);
        for (size_t i = 0; i < FRAMES; ++i) sum_sq += double(buf[i * 2]) * double(buf[i * 2]);
    }
    float rms = float(std::sqrt(sum_sq / double(FRAMES * 5)));
    std::cout << "[AcidReverb] Onset RMS (5 blocks): " << rms << "\n";
    EXPECT_GT(rms, 0.001f) << "Expected non-silent acid pluck at onset";

    engine_note_off(engine(), 36);
}

// ---------------------------------------------------------------------------
// Test 2: ReverbTailPersists — FDN reverb keeps signal alive after note-off
//
// Protocol:
//   - note_on(C2), run 12 blocks (~128ms, attack+decay complete, sustain=0)
//   - note_off
//   - run 20 more blocks (~213ms) and measure RMS
//   - FDN rt60=2s means energy decays by 60dB over 2s — at ~213ms it's very audible
// ---------------------------------------------------------------------------

TEST_F(AcidReverbPatchTest, ReverbTailPersists) {
    PRINT_TEST_HEADER(
        "Acid Reverb — Reverb Tail",
        "After note-off, FDN reverb (rt60=2s) keeps signal alive ~200ms later.",
        "note_on(C2) → 12 blocks → note_off → 20 blocks → assert RMS > 0.0005",
        "RMS > 0.0005 in the 20 blocks (~213ms) following note-off.",
        sample_rate
    );

    ASSERT_EQ(engine_load_patch(engine(), kPatch), 0);
    engine_note_on(engine(), 36, 1.0f);  // C2

    std::vector<float> buf(FRAMES * 2);

    // Let attack and decay complete (12 blocks ~ 128ms)
    for (int b = 0; b < 12; ++b)
        engine_process(engine(), buf.data(), FRAMES);

    engine_note_off(engine(), 36);

    // Measure 20 blocks after note-off
    double sum_sq = 0.0;
    for (int b = 0; b < 20; ++b) {
        engine_process(engine(), buf.data(), FRAMES);
        for (size_t i = 0; i < FRAMES; ++i) sum_sq += double(buf[i * 2]) * double(buf[i * 2]);
    }
    float rms = float(std::sqrt(sum_sq / double(FRAMES * 20)));
    std::cout << "[AcidReverb] Reverb tail RMS (~200ms after note-off): " << rms << "\n";
    EXPECT_GT(rms, 0.0005f) << "Expected FDN reverb tail to keep signal alive after note-off";
}

// ---------------------------------------------------------------------------
// Test 3: AcidSequenceAudible — staccato acid pattern with reverb wash
// ---------------------------------------------------------------------------

TEST_F(AcidReverbPatchTest, FmAcidRiffAudible) {
    PRINT_TEST_HEADER(
        "Acid Reverb — Fm Acid Riff (audible)",
        "4-bar Fm acid riff at 120 BPM on 16th-note grid. "
        "AD envelope sweeps diode filter for TB-303 style cutoff movement.",
        "engine_load_patch(acid_reverb.json) → engine_start → 4×16-step Fm riff",
        "Audible TB-style acid bass with filter wah and reverb wash.",
        sample_rate
    );

    ASSERT_EQ(engine_load_patch(engine(), kPatch), 0);
    ASSERT_ENGINE_START(engine());

    // 120 BPM: 16th note = 125ms, gate = 70ms (staccato TB-style)
    constexpr int SIXTEENTH_MS = 125;
    constexpr int GATE_MS      =  70;

    // 16-step Fm phrase (0 = rest). F2=41 G2=43 Ab2=44 Bb2=46 C3=48 Eb3=51 F3=53
    // Accented notes (odd indices in accent array) play at velocity 0.9, others 0.65
    struct Step { int midi; float vel; };
    const Step phrase[] = {
        {41, 0.9f}, {41, 0.65f}, {44, 0.65f}, { 0, 0.0f},
        {48, 0.65f}, {46, 0.65f}, {44, 0.65f}, {41, 0.9f},
        {41, 0.65f}, {51, 0.65f}, {48, 0.65f}, {46, 0.65f},
        {44, 0.9f},  {41, 0.65f}, {43, 0.65f}, {44, 0.65f}
    };
    constexpr int PHRASE_LEN = 16;
    constexpr int BARS       =  4;

    std::cout << "[AcidReverb] Playing 4-bar Fm acid riff at 120 BPM...\n";
    for (int bar = 0; bar < BARS; ++bar) {
        for (int s = 0; s < PHRASE_LEN; ++s) {
            const auto& step = phrase[s];
            if (step.midi > 0) {
                engine_note_on(engine(), step.midi, step.vel);
                std::this_thread::sleep_for(std::chrono::milliseconds(GATE_MS));
                engine_note_off(engine(), step.midi);
                std::this_thread::sleep_for(std::chrono::milliseconds(SIXTEENTH_MS - GATE_MS));
            } else {
                std::this_thread::sleep_for(std::chrono::milliseconds(SIXTEENTH_MS));
            }
        }
    }

    // Let reverb tail ring out
    std::this_thread::sleep_for(std::chrono::milliseconds(1500));
    engine_stop(engine());
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
