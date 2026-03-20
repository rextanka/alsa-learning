/**
 * @file test_tom_tom_patch.cpp
 * @brief Functional tests for tom_tom.json — dual-VCO FM tom with filter sweep.
 *
 * Patch topology (Phase 26 rework — fm_in enabled):
 *   VCO1 (triangle=1.0, sine=0.5) → SH_FILTER.audio_in  (tonal body layer)
 *   VCO2 (sine=0.4, transpose=7)  → SH_FILTER.fm_in     (FM modulator, P5 above)
 *   SH_FILTER (cutoff=280, res=0.72) → VCA ← AD_ENVELOPE (attack=1ms, decay=350ms)
 *   ENV → VCF.cutoff_cv (envelope sweeps filter down from peak)
 *
 * Characteristic features:
 *   - Fast 1ms attack for crisp percussive onset
 *   - 350ms decay gives the sustained tom body
 *   - VCO2 FM modulator adds transient click and pitched complexity at onset
 *   - Envelope sweeps VCF cutoff: bright attack, darker sustain
 *   - SH_FILTER (CEM) resonance at 0.72 gives the characteristic tonal ring
 *   - Lower notes produce lower resonance peak (kybd_cv tracking)
 *
 * Key assertions:
 *   1. NoteOnProducesAudio  — onset (5 blocks) RMS > 0.001.
 *   2. PercussiveDecay      — onset/tail RMS ratio >= 3.0.
 *   3. KeyboardTracking     — C2 centroid < C4 centroid (lower note = lower resonance peak).
 *   4. TomFillAudible       — alternating C3/E3 pattern at 100 BPM eighth notes.
 *
 * Timing at 48 kHz, block=512 (~10.67ms/block):
 *   ONSET: blocks 0-3   (~0-32ms)    — near envelope peak
 *   TAIL:  blocks 38-44 (~405-469ms) — deep into decay
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

class TomTomPatchTest : public ::testing::Test {
protected:
    void SetUp() override {
        test::init_test_environment();
        sample_rate = test::get_safe_sample_rate(0);
        engine_wrapper = std::make_unique<test::EngineWrapper>(sample_rate);
    }

    EngineHandle engine() { return engine_wrapper->get(); }

    int sample_rate;
    std::unique_ptr<test::EngineWrapper> engine_wrapper;

    static constexpr const char* kPatch = "patches/tom_tom.json";
    static constexpr size_t FRAMES = 512;
};

// ---------------------------------------------------------------------------
// Test 1: NoteOnProducesAudio — tom onset is non-silent
// ---------------------------------------------------------------------------

TEST_F(TomTomPatchTest, NoteOnProducesAudio) {
    PRINT_TEST_HEADER(
        "Tom Tom — Smoke",
        "Note-on(C3) produces non-silent audio at onset.",
        "engine_load_patch(tom_tom.json) → note_on(C3=48) → engine_process × 5",
        "RMS > 0.001 within first 5 blocks.",
        sample_rate
    );

    ASSERT_EQ(engine_load_patch(engine(), kPatch), 0);
    engine_note_on(engine(), 48, 1.0f);  // C3

    std::vector<float> buf(FRAMES * 2);
    double sum_sq = 0.0;
    for (int b = 0; b < 5; ++b) {
        engine_process(engine(), buf.data(), FRAMES);
        for (size_t i = 0; i < FRAMES; ++i) sum_sq += double(buf[i * 2]) * double(buf[i * 2]);
    }
    float rms = float(std::sqrt(sum_sq / double(FRAMES * 5)));
    std::cout << "[TomTom] Onset RMS (5 blocks): " << rms << "\n";
    EXPECT_GT(rms, 0.001f) << "Expected non-silent tom output at onset";

    engine_note_off(engine(), 48);
}

// ---------------------------------------------------------------------------
// Test 2: PercussiveDecay — onset RMS significantly higher than tail RMS
//
// ONSET: blocks 0-3  (~0-32ms)    — near envelope peak (attack=1ms)
// TAIL:  blocks 58-64 (~619-683ms) — deep into 500ms decay tail
// Expected: rms_onset / rms_tail >= 3.0
// ---------------------------------------------------------------------------

TEST_F(TomTomPatchTest, PercussiveDecay) {
    PRINT_TEST_HEADER(
        "Tom Tom — Percussive Decay",
        "AD decay=500ms: onset RMS (blocks 0-3) is >=3x the tail RMS (blocks 58-64).",
        "note_on(C3) → capture onset blocks 0-3 and tail blocks 58-64 → compare ratio",
        "rms_onset / rms_tail >= 3.0",
        sample_rate
    );

    ASSERT_EQ(engine_load_patch(engine(), kPatch), 0);
    engine_note_on(engine(), 48, 1.0f);  // C3

    std::vector<float> buf(FRAMES * 2);
    double onset_sq = 0.0; int onset_n = 0;
    double tail_sq  = 0.0; int tail_n  = 0;

    const int ONSET_START = 0;
    const int ONSET_END   = 3;
    const int TAIL_START  = 58;
    const int TAIL_END    = 64;

    for (int b = 0; b < TAIL_END; ++b) {
        engine_process(engine(), buf.data(), FRAMES);
        double block_sq = 0.0;
        for (size_t i = 0; i < FRAMES; ++i) block_sq += double(buf[i * 2]) * double(buf[i * 2]);
        if (b >= ONSET_START && b < ONSET_END) { onset_sq += block_sq; ++onset_n; }
        if (b >= TAIL_START  && b < TAIL_END)  { tail_sq  += block_sq; ++tail_n;  }
    }
    engine_note_off(engine(), 48);

    float rms_onset = float(std::sqrt(onset_sq / double(FRAMES * onset_n)));
    float rms_tail  = float(std::sqrt(tail_sq  / double(FRAMES * tail_n)));
    float ratio     = rms_tail > 1e-6f ? rms_onset / rms_tail : 0.0f;

    std::cout << "[TomTom] RMS onset (blocks 0-3):   " << rms_onset << "\n";
    std::cout << "[TomTom] RMS tail  (blocks 58-64): " << rms_tail  << "\n";
    std::cout << "[TomTom] Ratio onset/tail:          " << ratio     << "\n";

    EXPECT_GE(ratio, 3.0f)
        << "Expected percussive onset/tail ratio >= 3.0 "
        << "(onset=" << rms_onset << " tail=" << rms_tail << ")";
}

// ---------------------------------------------------------------------------
// Test 3: KeyboardTracking — lower note = lower spectral centroid
//
// The SH_FILTER cutoff_cv is swept by the envelope. The base cutoff=240Hz
// tracks note pitch via kybd_cv injection. C2 (65.4 Hz) produces a lower
// resonance peak than C4 (261.6 Hz).
//
// Two separate EngineWrapper instances for clean per-note measurement.
// Capture 2048 samples from blocks 1-4 per note.
// ---------------------------------------------------------------------------

TEST_F(TomTomPatchTest, KeyboardTracking) {
    PRINT_TEST_HEADER(
        "Tom Tom — Keyboard Tracking (automated)",
        "Lower note (C2) has lower spectral centroid than higher note (C4), "
        "confirming kybd_cv tracking shifts the filter resonance peak with pitch.",
        "separate engine instances → C2 vs C4 → centroid(C4) > centroid(C2) * 1.4",
        "centroid_high (C4) > centroid_low (C2) * 1.4",
        sample_rate
    );

    const size_t BLOCK  = 512;
    const size_t WINDOW = 2048;

    // Measure C2
    float centroid_low = 0.0f;
    {
        test::EngineWrapper eng_low(sample_rate);
        ASSERT_EQ(engine_load_patch(eng_low.get(), kPatch), 0);
        engine_note_on(eng_low.get(), 36, 1.0f);  // C2

        std::vector<float> buf(BLOCK * 2);
        std::vector<float> mono;
        mono.reserve(WINDOW);

        for (int b = 0; b < 5; ++b) {
            engine_process(eng_low.get(), buf.data(), BLOCK);
            if (b >= 1 && mono.size() < WINDOW) {
                for (size_t i = 0; i < BLOCK && mono.size() < WINDOW; ++i)
                    mono.push_back(buf[i * 2]);
            }
        }
        engine_note_off(eng_low.get(), 36);
        ASSERT_EQ(mono.size(), WINDOW);
        centroid_low = spectral_centroid(mono, sample_rate);
    }

    // Measure C4
    float centroid_high = 0.0f;
    {
        test::EngineWrapper eng_high(sample_rate);
        ASSERT_EQ(engine_load_patch(eng_high.get(), kPatch), 0);
        engine_note_on(eng_high.get(), 60, 1.0f);  // C4

        std::vector<float> buf(BLOCK * 2);
        std::vector<float> mono;
        mono.reserve(WINDOW);

        for (int b = 0; b < 5; ++b) {
            engine_process(eng_high.get(), buf.data(), BLOCK);
            if (b >= 1 && mono.size() < WINDOW) {
                for (size_t i = 0; i < BLOCK && mono.size() < WINDOW; ++i)
                    mono.push_back(buf[i * 2]);
            }
        }
        engine_note_off(eng_high.get(), 60);
        ASSERT_EQ(mono.size(), WINDOW);
        centroid_high = spectral_centroid(mono, sample_rate);
    }

    std::cout << "[TomTom] Spectral centroid C2: " << centroid_low  << " Hz\n";
    std::cout << "[TomTom] Spectral centroid C4: " << centroid_high << " Hz\n";

    EXPECT_GT(centroid_high, centroid_low * 1.4f)
        << "Expected C4 centroid > C2 centroid * 1.4 (kybd_cv tracking). "
        << "C2=" << centroid_low << " Hz  C4=" << centroid_high << " Hz";
}

// ---------------------------------------------------------------------------
// Test 4: TomFillAudible — alternating C3/E3 at 100 BPM eighth notes
// ---------------------------------------------------------------------------

TEST_F(TomTomPatchTest, TomFillAudible) {
    PRINT_TEST_HEADER(
        "Tom Tom — Fill Pattern (audible)",
        "Alternating C3/E3 at 100 BPM eighth notes. "
        "Pattern: C3 E3 C3 E3 C3 C3 E3 E3 (8 eighth notes).",
        "engine_load_patch(tom_tom.json) → engine_start → 8-note fill pattern",
        "Audible percussive tom pattern with pitch variation.",
        sample_rate
    );

    ASSERT_EQ(engine_load_patch(engine(), kPatch), 0);
    ASSERT_ENGINE_START(engine());

    constexpr int EIGHTH_MS = 300;  // 100 BPM eighth note
    constexpr int GATE_MS   =  30;  // short percussive gate

    const int pattern[] = {48, 52, 48, 52, 48, 48, 52, 52};  // C3, E3, C3, E3, C3, C3, E3, E3
    std::cout << "[TomTom] Playing tom fill (C3/E3 at 100 BPM eighths)...\n";
    for (int n : pattern) {
        engine_note_on(engine(), n, 0.9f);
        std::this_thread::sleep_for(std::chrono::milliseconds(GATE_MS));
        engine_note_off(engine(), n);
        std::this_thread::sleep_for(std::chrono::milliseconds(EIGHTH_MS - GATE_MS));
    }

    // Let last decay ring out
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    engine_stop(engine());
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
