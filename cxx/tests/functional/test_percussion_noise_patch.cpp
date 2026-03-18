/**
 * @file test_percussion_noise_patch.cpp
 * @brief Functional tests for percussion_noise.json.
 *
 * Patch topology:
 *   WHITE_NOISE → VCA ← ENV (attack=1ms, decay=180ms, sustain=0, release=80ms)
 *
 * Fully percussive: sustain=0 means the envelope decays completely to silence
 * after each strike. No filter is in the audio chain.
 *
 * ADSR IIR timing at 48kHz (coeff = exp(-log9 / (T * sr))):
 *   attack=1ms:   full level by first audio block (~10ms)
 *   decay=180ms:  after 360ms, envelope ≈ (1/9)^2 ≈ 0.012  (near silence)
 *
 * Tests:
 *   1. Smoke         — patch loads and note-on produces non-silent audio.
 *   2. PercussiveDecay — onset RMS (block 0) is ≥5× the RMS at ~400ms,
 *                        confirming sustain=0 decays the signal to near-silence.
 *   3. FourFourPattern — four hits at 80 BPM (750ms spacing) in a 4/4 grid,
 *                        each producing a non-silent onset transient. Audible.
 */

#include <gtest/gtest.h>
#include "../TestHelper.hpp"
#include <vector>
#include <cmath>
#include <thread>
#include <chrono>

// ---------------------------------------------------------------------------
// Fixture
// ---------------------------------------------------------------------------

class PercussionNoisePatchTest : public ::testing::Test {
protected:
    void SetUp() override {
        test::init_test_environment();
        sample_rate = test::get_safe_sample_rate(0);
        engine_wrapper = std::make_unique<test::EngineWrapper>(sample_rate);
    }

    EngineHandle engine() { return engine_wrapper->get(); }

    int sample_rate;
    std::unique_ptr<test::EngineWrapper> engine_wrapper;

    static constexpr const char* kPatch = "patches/percussion_noise.json";
};

// ---------------------------------------------------------------------------
// Test 1: Smoke — patch loads and produces audio
// ---------------------------------------------------------------------------

TEST_F(PercussionNoisePatchTest, NoteOnProducesAudio) {
    PRINT_TEST_HEADER(
        "Percussion Noise — Smoke",
        "Note-on produces non-silent audio through NOISE → VCA chain.",
        "engine_load_patch(percussion_noise.json) → note_on → engine_process × 3",
        "RMS > 0.001 across first 3 blocks (1ms attack completes within block 0).",
        sample_rate
    );

    ASSERT_EQ(engine_load_patch(engine(), kPatch), 0);
    engine_note_on(engine(), 60, 1.0f);

    const size_t FRAMES = 512;
    std::vector<float> buf(FRAMES * 2);
    double sum_sq = 0.0;
    for (int b = 0; b < 3; ++b) {
        engine_process(engine(), buf.data(), FRAMES);
        for (size_t i = 0; i < FRAMES; ++i) sum_sq += double(buf[i * 2]) * double(buf[i * 2]);
    }
    float rms = float(std::sqrt(sum_sq / double(FRAMES * 3)));
    std::cout << "[PercNoise] Onset RMS: " << rms << "\n";
    EXPECT_GT(rms, 0.001f) << "Expected non-silent onset";

    engine_note_off(engine(), 60);
}

// ---------------------------------------------------------------------------
// Test 2: PercussiveDecay — signal decays to near-silence (sustain=0)
//
// ENV: decay=180ms, sustain=0.
//   After one decay time constant (180ms): envelope ≈ 1/9 ≈ 0.111
//   After two time constants   (360ms): envelope ≈ (1/9)^2 ≈ 0.012
//
// ONSET window: blocks 0–2  (~0–32ms)   — envelope at peak
// TAIL  window: blocks 37–42 (~395–448ms) — envelope ≈ 0.003–0.005
//
// Expected ratio onset/tail ≥ 5.
// ---------------------------------------------------------------------------

TEST_F(PercussionNoisePatchTest, PercussiveDecay) {
    PRINT_TEST_HEADER(
        "Percussion Noise — Percussive Decay (automated)",
        "Onset RMS (~0–32ms) is ≥5× the tail RMS (~395–448ms), confirming "
        "the sustain=0 envelope decays the noise to near-silence.",
        "engine_load_patch → note_on → engine_process → compare onset vs tail RMS",
        "rms_onset / rms_tail ≥ 5.0",
        sample_rate
    );

    ASSERT_EQ(engine_load_patch(engine(), kPatch), 0);
    engine_note_on(engine(), 60, 1.0f);

    const size_t FRAMES      = 512;
    const int    ONSET_START = 0;
    const int    ONSET_END   = 3;    // ~0–32ms
    const int    TAIL_START  = 37;
    const int    TAIL_END    = 43;   // ~395–459ms

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
    engine_note_off(engine(), 60);

    float rms_onset = float(std::sqrt(onset_sq / double(FRAMES * onset_n)));
    float rms_tail  = float(std::sqrt(tail_sq  / double(FRAMES * tail_n)));
    float ratio     = rms_tail > 1e-9f ? rms_onset / rms_tail : 0.0f;

    std::cout << "[PercNoise] RMS onset (~0–32ms):   " << rms_onset << "\n";
    std::cout << "[PercNoise] RMS tail  (~395–459ms): " << rms_tail  << "\n";
    std::cout << "[PercNoise] Ratio onset/tail:        " << ratio     << "\n";

    EXPECT_GT(rms_onset, 0.001f) << "No signal at onset — envelope may not be triggering";
    EXPECT_GT(ratio, 5.0f)
        << "Expected onset/tail ratio ≥ 5 (sustain=0 decays to near-silence); got " << ratio;
}

// ---------------------------------------------------------------------------
// Test 3: FourFourPattern — four hits at 80 BPM with per-hit onset check + audible
//
// 80 BPM: quarter note = 750ms. Gate=10ms (percussive trigger), ring out 740ms.
// Each hit is verified to produce a non-silent onset before playing live.
// ---------------------------------------------------------------------------

TEST_F(PercussionNoisePatchTest, FourFourPatternAudible) {
    PRINT_TEST_HEADER(
        "Percussion Noise — 4/4 at 80 BPM (audible)",
        "Four percussive noise hits on the quarter-note grid at 80 BPM. "
        "Each hit onset is verified non-silent, then played live.",
        "engine_load_patch → automated onset check → engine_start → 4 hits × 750ms",
        "Per-hit onset RMS > 0.001; audible 4/4 noise hits.",
        sample_rate
    );

    ASSERT_EQ(engine_load_patch(engine(), kPatch), 0);

    // --- Automated: verify each of the 4 hits produces a non-silent onset ---
    const size_t FRAMES         = 512;
    const size_t ONSET_BLOCKS   = 3;     // ~32ms — captures the onset peak
    const size_t BETWEEN_BLOCKS = 67;    // ~715ms gap between hits at 48kHz/512
    std::vector<float> buf(FRAMES * 2);

    for (int hit = 0; hit < 4; ++hit) {
        engine_note_on(engine(), 60, 1.0f);
        double sum_sq = 0.0;
        for (size_t b = 0; b < ONSET_BLOCKS; ++b) {
            engine_process(engine(), buf.data(), FRAMES);
            for (size_t i = 0; i < FRAMES; ++i) sum_sq += double(buf[i * 2]) * double(buf[i * 2]);
        }
        engine_note_off(engine(), 60);
        for (size_t b = 0; b < BETWEEN_BLOCKS; ++b)
            engine_process(engine(), buf.data(), FRAMES);

        float rms = float(std::sqrt(sum_sq / double(FRAMES * ONSET_BLOCKS)));
        EXPECT_GT(rms, 0.001f) << "Hit " << hit + 1 << " produced no onset signal";
    }

    // --- Audible: replay the same pattern live ---
    ASSERT_EQ(engine_start(engine()), 0);

    constexpr int BEAT_MS = 750;
    constexpr int GATE_MS =  10;

    std::cout << "[PercNoise] Playing 4/4 at 80 BPM (4 hits)…\n";
    for (int i = 0; i < 4; ++i) {
        engine_note_on(engine(), 60, 1.0f);
        std::this_thread::sleep_for(std::chrono::milliseconds(GATE_MS));
        engine_note_off(engine(), 60);
        std::this_thread::sleep_for(std::chrono::milliseconds(BEAT_MS - GATE_MS));
    }

    // Let the last hit's decay tail finish
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    engine_stop(engine());
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
