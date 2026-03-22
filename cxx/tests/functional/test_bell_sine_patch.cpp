/**
 * @file test_bell_sine_patch.cpp
 * @brief Functional tests for bell_sine.json — bell using sine-approximated VCOs.
 *
 * Patch topology (Roland System 100M Fig. 2-18):
 *   VCO1 (triangle, 4') → VCF1 (cutoff=3kHz, sine approx) → RING_MOD.audio_in_a
 *   VCO2 (triangle, 4', minor-3rd above VCO1) → VCF2 (cutoff=3kHz) → RING_MOD.audio_in_b
 *   RING_MOD → VCA ← ADSR (attack=1ms, decay=1.2s, sustain=0, release=0.8s)
 *
 * Each VCO triangle wave is filtered to approximate a sine before entering the
 * ring modulator. Pure ring-mod output only — no direct VCO paths.
 * Compare with bell.json (Fig. 2-17) which mixes direct VCO + RM.
 *
 * Ring-mod with sine-approximated inputs produces cleaner sidebands than the
 * triangle-based Fig. 2-17 patch. Centroid should be well above the fundamental.
 *
 * Key assertions:
 *   1. Smoke         — patch loads and note-on produces audio.
 *   2. SidebandOnly  — centroid > 1.3× fundamental (pure RM sidebands, no direct VCO).
 *   3. PercussiveDecay — RMS drops significantly after 1.2s decay.
 *   4. Audible       — C4/E4/G4/C5 arpeggio played live.
 *
 * Timing at 48 kHz, block=512 (~10.67ms/block):
 *   decay=1.2s ≈ 112 blocks
 *   Onset: blocks 1–3  (~10–32ms)
 *   Tail:  blocks 108–116 (~1152–1237ms, env ≈ 0.111 of peak)
 */

#include <gtest/gtest.h>
#include "../TestHelper.hpp"
#include "PatchAnalysis.hpp"
#include <vector>
#include <cmath>
#include <thread>
#include <chrono>

class BellSinePatchTest : public ::testing::Test {
protected:
    void SetUp() override {
        test::init_test_environment();
        sample_rate = test::get_safe_sample_rate(0);
        engine_wrapper = std::make_unique<test::EngineWrapper>(sample_rate);
    }
    EngineHandle engine() { return engine_wrapper->get(); }
    int sample_rate;
    std::unique_ptr<test::EngineWrapper> engine_wrapper;
    static constexpr const char* kPatch = "patches/bell_sine.json";
};

// ---------------------------------------------------------------------------
// Test 1: Smoke
// ---------------------------------------------------------------------------

TEST_F(BellSinePatchTest, NoteOnProducesAudio) {
    PRINT_TEST_HEADER(
        "Bell Sine — Smoke",
        "Triangle VCOs filtered to sine, ring-modulated — produces non-silent audio.",
        "engine_load_patch(bell_sine.json) → note_on(A4) → 5 blocks",
        "RMS > 0.001",
        sample_rate
    );

    ASSERT_EQ(engine_load_patch(engine(), kPatch), 0);
    engine_note_on(engine(), 69, 1.0f);  // A4

    const size_t FRAMES = 512;
    std::vector<float> buf(FRAMES * 2);
    double sum_sq = 0.0;
    for (int b = 0; b < 5; ++b) {
        engine_process(engine(), buf.data(), FRAMES);
        for (size_t i = 0; i < FRAMES; ++i) sum_sq += double(buf[i * 2]) * double(buf[i * 2]);
    }
    engine_note_off(engine(), 69);

    float rms = float(std::sqrt(sum_sq / double(FRAMES * 5)));
    std::cout << "[BellSine] 5-block onset RMS: " << rms << "\n";
    EXPECT_GT(rms, 0.001f);
}

// ---------------------------------------------------------------------------
// Test 2: SidebandOnly — pure RM output, centroid above fundamental
//
// For A4 (440 Hz) at 4' footage: VCO1 ≈ 1760 Hz (A6), VCO2 minor-3rd above ≈ 2093 Hz (C7)
// Ring-mod sidebands: sum ≈ 3853 Hz, diff ≈ 333 Hz
// VCO direct at fundamental (440 Hz) is absent — pure RM sidebands only.
// Expected: centroid > 440 × 1.3 = 572 Hz.
// ---------------------------------------------------------------------------

TEST_F(BellSinePatchTest, SidebandOnly) {
    PRINT_TEST_HEADER(
        "Bell Sine — Sideband Content (automated)",
        "Pure RM output: centroid > 1.3× keyboard fundamental (440 Hz).",
        "engine_load_patch → note_on(A4) → capture 2048-sample onset → centroid",
        "centroid > 440 × 1.3 = 572 Hz",
        sample_rate
    );

    ASSERT_EQ(engine_load_patch(engine(), kPatch), 0);
    engine_note_on(engine(), 69, 1.0f);  // A4 = 440 Hz

    const size_t BLOCK  = 512;
    const size_t WINDOW = 2048;
    std::vector<float> buf(BLOCK * 2);
    std::vector<float> mono;
    mono.reserve(WINDOW);

    for (int b = 0; b < 8; ++b) {
        engine_process(engine(), buf.data(), BLOCK);
        if (b >= 2 && mono.size() < WINDOW)
            for (size_t i = 0; i < BLOCK && mono.size() < WINDOW; ++i)
                mono.push_back(buf[i * 2]);
    }
    engine_note_off(engine(), 69);

    ASSERT_EQ(mono.size(), WINDOW);

    constexpr float kA4Hz = 440.0f;
    const float centroid = spectral_centroid(mono, sample_rate);

    std::cout << "[BellSine] A4 fundamental:    " << kA4Hz    << " Hz\n";
    std::cout << "[BellSine] Spectral centroid: " << centroid << " Hz\n";
    std::cout << "[BellSine] Centroid / fund.:  " << centroid / kA4Hz << "×\n";

    EXPECT_GT(centroid, kA4Hz * 1.3f)
        << "Expected centroid > " << kA4Hz * 1.3f << " Hz; got " << centroid << " Hz";
}

// ---------------------------------------------------------------------------
// Test 3: PercussiveDecay
// ---------------------------------------------------------------------------

TEST_F(BellSinePatchTest, PercussiveDecay) {
    PRINT_TEST_HEADER(
        "Bell Sine — Percussive Decay (automated)",
        "Onset RMS (~10–32ms) ≥ 5× tail RMS (~1152–1237ms, ADSR decay=1.2s, S=0).",
        "engine_load_patch → note_on(A4) → 116 blocks → compare onset vs tail RMS",
        "rms_onset / rms_tail ≥ 5.0",
        sample_rate
    );

    ASSERT_EQ(engine_load_patch(engine(), kPatch), 0);
    engine_note_on(engine(), 69, 1.0f);  // A4

    const size_t FRAMES      = 512;
    const int    ONSET_START = 1;
    const int    ONSET_END   = 4;
    const int    TAIL_START  = 108;
    const int    TAIL_END    = 116;

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

    std::cout << "[BellSine] RMS onset (~10–32ms):     " << rms_onset << "\n";
    std::cout << "[BellSine] RMS tail  (~1152–1237ms): " << rms_tail  << "\n";
    std::cout << "[BellSine] Ratio onset/tail:          " << ratio     << "\n";

    EXPECT_GT(rms_onset, 0.001f) << "No signal at onset";
    EXPECT_GT(ratio, 5.0f) << "Expected onset/tail ≥ 5; got " << ratio;
}

// ---------------------------------------------------------------------------
// Test 4: Audible
// ---------------------------------------------------------------------------

TEST_F(BellSinePatchTest, BellSinePatternAudible) {
    PRINT_TEST_HEADER(
        "Bell Sine — C4/E4/G4/C5 Pattern (audible)",
        "Sine-approximated ring-mod bell — cleaner, purer than Fig. 2-17. "
        "Compare timbre with bell.json.",
        "engine_load_patch(bell_sine.json) → engine_start → C4/E4/G4/C5",
        "Audible pure bell timbre with 1.2s decay.",
        sample_rate
    );

    ASSERT_EQ(engine_load_patch(engine(), kPatch), 0);
    ASSERT_ENGINE_START(engine());

    constexpr int NOTE_MS    =  80;
    constexpr int RELEASE_MS = 1800;

    const int notes[] = {60, 64, 67, 72};  // C4, E4, G4, C5
    std::cout << "[BellSine] Playing C4 → E4 → G4 → C5…\n";
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
