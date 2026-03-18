/**
 * @file test_cymbal_patch.cpp
 * @brief Functional tests for cymbal.json.
 *
 * Patch topology:
 *   WHITE_NOISE → VCF (MOOG_FILTER, cutoff=6kHz, hpf_cutoff=2)
 *              → DLY (ECHO_DELAY, 25ms, fb=0.55, mix=0.5, LFO 6.5Hz/40%)
 *              → VCA ← ENV (attack=1ms, decay=350ms, sustain=0, release=150ms)
 *
 * Tests:
 *   1. Smoke        — note-on produces non-silent signal.
 *   2. Echo tail    — signal persists after note-off (delay feedback + release).
 *   3. Spectral shape — Moog LP at 6kHz collapses the white-noise centroid well
 *                      below the flat-noise midpoint of ~12kHz.
 *   4. Shimmer      — delay LFO (6.5 Hz, ±40% depth) causes measurable per-block
 *                      RMS variation during the decay tail.
 *   5. Audible      — four hits in a 4/4 pattern at 80 BPM.
 */

#include <gtest/gtest.h>
#include "../TestHelper.hpp"
#include "PatchAnalysis.hpp"
#include <vector>
#include <cmath>
#include <numeric>
#include <thread>
#include <chrono>

// ---------------------------------------------------------------------------
// Fixture
// ---------------------------------------------------------------------------

class CymbalPatchTest : public ::testing::Test {
protected:
    void SetUp() override {
        test::init_test_environment();
        sample_rate = test::get_safe_sample_rate(0);
        engine_wrapper = std::make_unique<test::EngineWrapper>(sample_rate);
    }

    EngineHandle engine() { return engine_wrapper->get(); }

    int sample_rate;
    std::unique_ptr<test::EngineWrapper> engine_wrapper;

    static constexpr const char* kPatch = "patches/cymbal.json";

    // Helpers
    static float block_rms(const std::vector<float>& buf, size_t frames) {
        double sum = 0.0;
        for (size_t i = 0; i < frames; ++i) sum += double(buf[i * 2]) * double(buf[i * 2]);
        return float(std::sqrt(sum / double(frames)));
    }
};

// ---------------------------------------------------------------------------
// Test 1: Smoke — note-on produces non-silent signal
// ---------------------------------------------------------------------------

TEST_F(CymbalPatchTest, NoteOnProducesAudio) {
    PRINT_TEST_HEADER(
        "Cymbal — Smoke",
        "Note-on produces non-silent audio through NOISE → VCF → DLY → VCA chain.",
        "engine_load_patch(cymbal.json) → note_on → engine_process × 10",
        "RMS > 0.001 within 10 × 512-frame blocks.",
        sample_rate
    );

    ASSERT_EQ(engine_load_patch(engine(), kPatch), 0);
    engine_note_on(engine(), 60, 0.9f);

    const size_t FRAMES = 512;
    std::vector<float> buf(FRAMES * 2);
    double sum_sq = 0.0;
    for (int b = 0; b < 10; ++b) {
        engine_process(engine(), buf.data(), FRAMES);
        for (size_t i = 0; i < FRAMES; ++i) sum_sq += double(buf[i * 2]) * double(buf[i * 2]);
    }
    float rms = float(std::sqrt(sum_sq / double(FRAMES * 10)));
    std::cout << "[Cymbal] Onset RMS: " << rms << "\n";
    EXPECT_GT(rms, 0.001f) << "Expected non-silent onset";

    engine_note_off(engine(), 60);
}

// ---------------------------------------------------------------------------
// Test 2: Echo tail — signal persists after note-off
//
// ENV decay reaches ~0.28 at 200ms from note-on (ADSR IIR: time constant 350ms).
// After note-off, release (150ms time constant → ~470ms to reach 0) keeps VCA
// open. Delay feedback (0.55) keeps the delay buffer alive.
// RMS at 100ms after note-off should be non-trivial.
// ---------------------------------------------------------------------------

TEST_F(CymbalPatchTest, EchoTailPersistsAfterNoteOff) {
    PRINT_TEST_HEADER(
        "Cymbal — Echo Tail",
        "Signal persists after note-off: delay feedback (fb=0.55) + ENV release keep "
        "audio alive beyond the initial transient.",
        "note_on → 18 blocks on → note_off → 10 blocks off → assert RMS > 0",
        "RMS > 0.001 in the 10 blocks (~107ms) immediately after note-off.",
        sample_rate
    );

    ASSERT_EQ(engine_load_patch(engine(), kPatch), 0);

    const size_t FRAMES          = 512;
    const int    NOTE_ON_BLOCKS  = 18;   // ~192ms: ENV at ~0.28, delay fully primed
    const int    TAIL_BLOCKS     = 10;   // ~107ms after note-off: ENV release ~0.06
    std::vector<float> buf(FRAMES * 2);

    engine_note_on(engine(), 60, 0.9f);
    for (int b = 0; b < NOTE_ON_BLOCKS; ++b)
        engine_process(engine(), buf.data(), FRAMES);

    engine_note_off(engine(), 60);

    double sum_sq = 0.0;
    for (int b = 0; b < TAIL_BLOCKS; ++b) {
        engine_process(engine(), buf.data(), FRAMES);
        for (size_t i = 0; i < FRAMES; ++i) sum_sq += double(buf[i * 2]) * double(buf[i * 2]);
    }
    float tail_rms = float(std::sqrt(sum_sq / double(FRAMES * TAIL_BLOCKS)));
    std::cout << "[Cymbal] Tail RMS (~100ms after note-off): " << tail_rms << "\n";
    EXPECT_GT(tail_rms, 0.001f)
        << "Expected signal after note-off — delay tail may not be feeding back";
}

// ---------------------------------------------------------------------------
// Test 3: Spectral shape — Moog LP collapses white-noise centroid
//
// Flat white noise at 48kHz has spectral centroid at Nyquist/2 = 24000 Hz
// (DCT-II midpoint). The Moog 4-pole LP at 6kHz rolls off energy above 6kHz.
// After filtering, the centroid should be well below 6000 Hz.
// ---------------------------------------------------------------------------

TEST_F(CymbalPatchTest, SpectralShapeBelowCutoff) {
    PRINT_TEST_HEADER(
        "Cymbal — Spectral Shape",
        "Moog LP at 6kHz shapes white noise: DCT centroid of output is well below "
        "the flat-noise midpoint (~12kHz), confirming the filter is active.",
        "note_on → capture 2048 samples → DCT centroid < 6000 Hz",
        "centroid < 6000 Hz (filter rolls off high-frequency noise energy).",
        sample_rate
    );

    ASSERT_EQ(engine_load_patch(engine(), kPatch), 0);
    engine_note_on(engine(), 60, 0.9f);

    const size_t BLOCK  = 512;
    const size_t WINDOW = 2048;
    std::vector<float> mono;
    mono.reserve(WINDOW);
    std::vector<float> buf(BLOCK * 2);

    // Skip first 2 blocks (attack transient), then capture WINDOW samples
    for (int b = 0; b < 6; ++b) {
        engine_process(engine(), buf.data(), BLOCK);
        if (b >= 2) {
            for (size_t i = 0; i < BLOCK && mono.size() < WINDOW; ++i)
                mono.push_back(buf[i * 2]);
        }
    }
    engine_note_off(engine(), 60);

    ASSERT_EQ(mono.size(), WINDOW);

    float centroid = spectral_centroid(mono, sample_rate);

    std::cout << "[Cymbal] Filtered-noise centroid: " << centroid << " Hz"
              << " (flat-noise midpoint would be ~" << sample_rate / 2 << " Hz)\n";
    EXPECT_LT(centroid, 6000.0f)
        << "Centroid " << centroid << " Hz is above the 6kHz LP cutoff — "
        << "Moog filter may not be shaping the noise";
}

// ---------------------------------------------------------------------------
// Test 4: Shimmer — LFO delay modulation causes per-block RMS variation
//
// ECHO_DELAY: mod_rate=6.5 Hz, mod_intensity=0.4 → delay time sweeps ±40%
// around 25ms (15ms–35ms) at 6.5 Hz. The sweeping comb filter + mix=0.5 create
// constructive/destructive interference that varies the output amplitude.
// Measure coefficient of variation (std_dev / mean) of per-block RMS across
// 40 blocks (~430ms) during the decay tail.
// ---------------------------------------------------------------------------

TEST_F(CymbalPatchTest, ShimmerCausesRmsVariation) {
    PRINT_TEST_HEADER(
        "Cymbal — Shimmer (LFO delay modulation)",
        "ECHO_DELAY LFO (6.5 Hz, ±40% depth) causes per-block RMS amplitude variation "
        "during the decay tail as the comb filter sweeps through the noise spectrum.",
        "note_on → skip 5 blocks → record 40 blocks → RMS coeff-of-variation > 3%",
        "std_dev(block_rms) / mean(block_rms) > 0.03.",
        sample_rate
    );

    ASSERT_EQ(engine_load_patch(engine(), kPatch), 0);
    engine_note_on(engine(), 60, 0.9f);

    const size_t FRAMES      = 512;
    const int    SKIP_BLOCKS = 5;    // skip attack transient (~53ms)
    const int    MEAS_BLOCKS = 40;   // ~430ms measurement window
    std::vector<float> buf(FRAMES * 2);
    std::vector<float> block_rms_vals;
    block_rms_vals.reserve(MEAS_BLOCKS);

    for (int b = 0; b < SKIP_BLOCKS + MEAS_BLOCKS; ++b) {
        engine_process(engine(), buf.data(), FRAMES);
        if (b >= SKIP_BLOCKS) {
            double sum = 0.0;
            for (size_t i = 0; i < FRAMES; ++i) sum += double(buf[i * 2]) * double(buf[i * 2]);
            block_rms_vals.push_back(float(std::sqrt(sum / double(FRAMES))));
        }
    }
    engine_note_off(engine(), 60);

    ASSERT_EQ(int(block_rms_vals.size()), MEAS_BLOCKS);

    float mean = 0.0f;
    for (float v : block_rms_vals) mean += v;
    mean /= float(MEAS_BLOCKS);

    float var = 0.0f;
    for (float v : block_rms_vals) var += (v - mean) * (v - mean);
    float std_dev = std::sqrt(var / float(MEAS_BLOCKS));
    float cv = mean > 1e-9f ? std_dev / mean : 0.0f;

    std::cout << "[Cymbal] Block RMS mean=" << mean
              << "  std_dev=" << std_dev
              << "  cv=" << cv << "\n";

    EXPECT_GT(mean, 0.001f) << "No signal during measurement window";
    EXPECT_GT(cv, 0.03f)
        << "Expected ≥3% RMS variation from LFO shimmer (cv=" << cv << ")";
}

// ---------------------------------------------------------------------------
// Test 5: Audible — four hits in 4/4 at 80 BPM
// ---------------------------------------------------------------------------

TEST_F(CymbalPatchTest, FourFourPatternAudible) {
    PRINT_TEST_HEADER(
        "Cymbal — 4/4 Pattern at 80 BPM (audible)",
        "Four cymbal strikes on the quarter-note grid at 80 BPM. "
        "Each hit triggers ENV (1ms attack, 350ms decay) through Moog LP + chorus delay.",
        "engine_load_patch(cymbal.json) → engine_start → 4 × note_on @ 750ms",
        "Audible metallic cymbal hits with shimmer tail, 4/4 at 80 BPM.",
        sample_rate
    );

    ASSERT_EQ(engine_load_patch(engine(), kPatch), 0);
    ASSERT_EQ(engine_start(engine()), 0);

    // 80 BPM: quarter note = 750ms. Short gate (percussive), ring out between hits.
    constexpr int BEAT_MS = 750;
    constexpr int GATE_MS = 25;

    std::cout << "[Cymbal] Playing 4/4 at 80 BPM (4 hits)…\n";
    for (int i = 0; i < 4; ++i) {
        engine_note_on(engine(), 60, 0.9f);
        std::this_thread::sleep_for(std::chrono::milliseconds(GATE_MS));
        engine_note_off(engine(), 60);
        std::this_thread::sleep_for(std::chrono::milliseconds(BEAT_MS - GATE_MS));
    }

    // Let the last hit's delay tail ring out
    std::this_thread::sleep_for(std::chrono::milliseconds(1500));
    engine_stop(engine());
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
