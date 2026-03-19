/**
 * @file test_bell_patch.cpp
 * @brief Functional tests for bell.json — ring modulator (RING_MOD).
 *
 * Patch topology:
 *   VCO1 (sine) → RING_MOD.audio_in_a
 *   VCO2 (sine, transpose=+19 semitones + detune=+63 cents ≈ 2.756×) → RING_MOD.audio_in_b
 *   RING_MOD → VCA ← ENV (attack=1ms, decay=1.2s, sustain=0, release=0.8s)
 *
 * Ring modulation output = A × B, producing only sum (A+B) and difference (A−B)
 * sidebands — neither matches VCO1 or VCO2 frequency alone. The result is an
 * inharmonic bell-like timbre.
 *
 * Key assertions:
 *   1. Smoke        — patch loads and note-on produces non-silent audio.
 *   2. SidebandOnly — the spectral centroid deviates significantly from VCO1's
 *                     frequency, confirming sideband content rather than a pure
 *                     fundamental. For A4 (440 Hz) and VCO2 at ≈1212 Hz, the
 *                     sidebands are at 1652 Hz (sum) and 772 Hz (diff), giving a
 *                     centroid well above the 440 Hz fundamental.
 *   3. PercussiveDecay — RMS drops to near-silence after 1.2s decay
 *                        (sustain=0 percussive bell decay).
 *   4. Audible      — bell pattern C4 / E4 / G4 / C5 played live.
 *
 * ADSR IIR at 48kHz:
 *   decay=1.2s: after 1.2s, envelope ≈ 1/9 ≈ 0.111 of peak.
 *   Window: ONSET blocks 1–3 (~10–32ms); TAIL blocks 108–116 (~1152–1237ms).
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

class BellPatchTest : public ::testing::Test {
protected:
    void SetUp() override {
        test::init_test_environment();
        sample_rate = test::get_safe_sample_rate(0);
        engine_wrapper = std::make_unique<test::EngineWrapper>(sample_rate);
    }

    EngineHandle engine() { return engine_wrapper->get(); }

    int sample_rate;
    std::unique_ptr<test::EngineWrapper> engine_wrapper;

    static constexpr const char* kPatch = "patches/bell.json";
};

// ---------------------------------------------------------------------------
// Test 1: Smoke — patch loads and ring-mod produces audio
// ---------------------------------------------------------------------------

TEST_F(BellPatchTest, NoteOnProducesAudio) {
    PRINT_TEST_HEADER(
        "Bell — Smoke",
        "Ring modulator with two VCOs (VCO1 × VCO2) produces non-silent audio.",
        "engine_load_patch(bell.json) → note_on(A4) → engine_process × 5",
        "RMS > 0.001 across 5 blocks.",
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
    float rms = float(std::sqrt(sum_sq / double(FRAMES * 5)));
    std::cout << "[Bell] 5-block onset RMS: " << rms << "\n";
    EXPECT_GT(rms, 0.001f) << "Expected non-silent ring-mod output";

    engine_note_off(engine(), 69);
}

// ---------------------------------------------------------------------------
// Test 2: SidebandOnly — spectral centroid deviates from the VCO1 fundamental
//
// For A4 (440 Hz):
//   VCO2 transpose=+19 semitones + detune=+63 cents ≈ +1963 cents ≈ 2.756× A4
//   VCO2 ≈ 1213 Hz
//   Ring-mod sidebands: sum = 440 + 1213 = 1653 Hz, diff = 1213 − 440 = 773 Hz
//   Spectral centroid of (1653, 773) ≈ ~1213 Hz (sum and diff weighted by amplitude)
//
// A pure A4 sine would give centroid = 440 Hz.
// Expected: centroid > 440 × 1.3 = 572 Hz (conservative threshold).
// ---------------------------------------------------------------------------

TEST_F(BellPatchTest, SidebandOnly) {
    PRINT_TEST_HEADER(
        "Bell — Sideband Content (automated)",
        "Ring-mod output DCT centroid is > 1.3× VCO1 fundamental (440 Hz), "
        "confirming inharmonic sideband content rather than a pure fundamental.",
        "engine_load_patch → note_on(A4) → skip 2 blocks → capture 2048 samples → centroid",
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

    // Skip 2 blocks (envelope rising), capture WINDOW samples
    for (int b = 0; b < 8; ++b) {
        engine_process(engine(), buf.data(), BLOCK);
        if (b >= 2 && mono.size() < WINDOW) {
            for (size_t i = 0; i < BLOCK && mono.size() < WINDOW; ++i)
                mono.push_back(buf[i * 2]);
        }
    }
    engine_note_off(engine(), 69);

    ASSERT_EQ(mono.size(), WINDOW);

    constexpr float kA4Hz = 440.0f;
    const float centroid = spectral_centroid(mono, sample_rate);

    std::cout << "[Bell] A4 fundamental:    " << kA4Hz    << " Hz\n";
    std::cout << "[Bell] Spectral centroid: " << centroid << " Hz\n";
    std::cout << "[Bell] Centroid / fund.:  " << centroid / kA4Hz << "×\n";

    EXPECT_GT(centroid, kA4Hz * 1.3f)
        << "Expected centroid > " << kA4Hz * 1.3f << " Hz (ring-mod sidebands); "
        << "got " << centroid << " Hz";
}

// ---------------------------------------------------------------------------
// Test 3: PercussiveDecay — bell fades to silence after 1.2s decay (sustain=0)
//
// IIR decay=1.2s: after one time constant, envelope ≈ 0.111.
// ONSET: blocks 1–3 (~10–32ms) — near peak
// TAIL:  blocks 108–116 (~1152–1237ms) — ≈ 0.111 of onset amplitude
// Expected ratio onset/tail ≥ 5.
// ---------------------------------------------------------------------------

TEST_F(BellPatchTest, PercussiveDecay) {
    PRINT_TEST_HEADER(
        "Bell — Percussive Decay (automated)",
        "Onset RMS (~10–32ms) is ≥5× the tail RMS (~1.15s), confirming "
        "sustain=0 decays the bell to near-silence.",
        "engine_load_patch → note_on(A4) → 116 blocks → compare onset vs tail RMS",
        "rms_onset / rms_tail ≥ 5.0",
        sample_rate
    );

    ASSERT_EQ(engine_load_patch(engine(), kPatch), 0);
    engine_note_on(engine(), 69, 1.0f);  // A4

    const size_t FRAMES      = 512;
    const int    ONSET_START = 1;
    const int    ONSET_END   = 4;    // ~10–32ms
    const int    TAIL_START  = 108;
    const int    TAIL_END    = 116;  // ~1152–1237ms

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

    std::cout << "[Bell] RMS onset (~10–32ms):   " << rms_onset << "\n";
    std::cout << "[Bell] RMS tail  (~1152–1237ms): " << rms_tail  << "\n";
    std::cout << "[Bell] Ratio onset/tail:          " << ratio     << "\n";

    EXPECT_GT(rms_onset, 0.001f) << "No signal at onset — ring-mod may not be running";
    EXPECT_GT(ratio, 5.0f)
        << "Expected onset/tail ratio ≥ 5 (sustain=0 percussive bell); got " << ratio;
}

// ---------------------------------------------------------------------------
// Test 4: Audible — bell pattern on C4 / E4 / G4 / C5
// ---------------------------------------------------------------------------

TEST_F(BellPatchTest, BellPatternAudible) {
    PRINT_TEST_HEADER(
        "Bell — C4/E4/G4/C5 Pattern (audible)",
        "Play a C-major arpeggio to hear the inharmonic bell partials and "
        "the 1.2s decay ring-out from the ring modulator.",
        "engine_load_patch(bell.json) → engine_start → C4/E4/G4/C5",
        "Audible metallic/bell timbre with slow decay and inharmonic partials.",
        sample_rate
    );

    ASSERT_EQ(engine_load_patch(engine(), kPatch), 0);
    ASSERT_ENGINE_START(engine());

    constexpr int NOTE_MS    =  80;  // short gate — percussive bell hit
    constexpr int RELEASE_MS = 1800; // 1.2s decay + margin

    const int notes[] = {60, 64, 67, 72};  // C4, E4, G4, C5
    std::cout << "[Bell] Playing C4 → E4 → G4 → C5…\n";
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
