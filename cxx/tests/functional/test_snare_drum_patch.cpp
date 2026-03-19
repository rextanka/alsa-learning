/**
 * @file test_snare_drum_patch.cpp
 * @brief Functional tests for snare_drum.json — filtered noise percussion.
 *
 * Patch topology (Practical Synthesis Vol. 2, §3-3 snare approximation):
 *   WHITE_NOISE → SH_FILTER (LP, cutoff=3500 Hz, res=0.4)
 *              → HIGH_PASS_FILTER (cutoff=180 Hz, res=0.1)
 *              → VCA ← AD_ENVELOPE (attack=1ms, decay=150ms)
 *
 * The two-filter cascade creates a bandpass effect: the LP removes harsh
 * high-frequency hiss while the HPF removes low-frequency rumble — leaving
 * the characteristic mid-frequency noise burst of a snare drum.
 *
 * Key assertions:
 *   1. Smoke         — note_on produces non-silent audio.
 *   2. PercussiveDecay — onset RMS >> tail RMS (decay=150ms, sustain=0).
 *   3. BandpassShape — centroid falls between HPF cutoff (180 Hz) and LP
 *                      cutoff (3500 Hz), confirming bandpass filtering.
 *   4. Audible       — 4/4 snare pattern at 90 BPM (beats 2 and 4).
 *
 * Timing at 48 kHz, block=512 (~10.67ms/block):
 *   decay=150ms ≈ 14 blocks.
 *   ONSET: blocks 0–2   (~0–21ms)    — near peak
 *   TAIL:  blocks 18–22 (~192–235ms) — ≈ e^(−1.28) ≈ 28% of onset level
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

class SnareDrumPatchTest : public ::testing::Test {
protected:
    void SetUp() override {
        test::init_test_environment();
        sample_rate = test::get_safe_sample_rate(0);
        engine_wrapper = std::make_unique<test::EngineWrapper>(sample_rate);
    }

    EngineHandle engine() { return engine_wrapper->get(); }

    int sample_rate;
    std::unique_ptr<test::EngineWrapper> engine_wrapper;

    static constexpr const char* kPatch = "patches/snare_drum.json";
};

// ---------------------------------------------------------------------------
// Test 1: Smoke — note-on produces non-silent audio
// ---------------------------------------------------------------------------

TEST_F(SnareDrumPatchTest, NoteOnProducesAudio) {
    PRINT_TEST_HEADER(
        "Snare Drum — Smoke",
        "WHITE_NOISE through LP+HPF cascade with AD envelope produces non-silent audio.",
        "engine_load_patch(snare_drum.json) → note_on → engine_process × 5",
        "RMS > 0.001 within first 5 blocks.",
        sample_rate
    );

    ASSERT_EQ(engine_load_patch(engine(), kPatch), 0);
    engine_note_on(engine(), 60, 1.0f);

    const size_t FRAMES = 512;
    std::vector<float> buf(FRAMES * 2);
    double sum_sq = 0.0;
    for (int b = 0; b < 5; ++b) {
        engine_process(engine(), buf.data(), FRAMES);
        for (size_t i = 0; i < FRAMES; ++i) sum_sq += double(buf[i * 2]) * double(buf[i * 2]);
    }
    float rms = float(std::sqrt(sum_sq / double(FRAMES * 5)));
    std::cout << "[SnareDrum] Onset RMS: " << rms << "\n";
    EXPECT_GT(rms, 0.001f) << "Expected non-silent onset from noise+filter chain";

    engine_note_off(engine(), 60);
}

// ---------------------------------------------------------------------------
// Test 2: PercussiveDecay — onset RMS > tail RMS × 4 (decay=150ms)
//
// After 150ms (decay time constant), envelope ≈ 1/9 ≈ 0.111.
// After 235ms (blocks 22), envelope ≈ e^(−235/150) ≈ 0.21 of peak.
// Expected onset/tail amplitude ratio ≥ 4.
// ---------------------------------------------------------------------------

TEST_F(SnareDrumPatchTest, PercussiveDecay) {
    PRINT_TEST_HEADER(
        "Snare Drum — Percussive Decay (automated)",
        "AD decay=150ms: onset RMS (~0–21ms) is ≥4× the tail RMS (~192–235ms).",
        "engine_load_patch → note_on → 22 blocks → compare onset vs tail RMS",
        "rms_onset / rms_tail ≥ 4.0",
        sample_rate
    );

    ASSERT_EQ(engine_load_patch(engine(), kPatch), 0);
    engine_note_on(engine(), 60, 1.0f);

    const size_t FRAMES      = 512;
    const int    ONSET_START = 0;
    const int    ONSET_END   = 3;   // ~0–32ms (peak)
    const int    TAIL_START  = 18;
    const int    TAIL_END    = 23;  // ~192–245ms (well into decay)

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

    std::cout << "[SnareDrum] RMS onset (~0–32ms):    " << rms_onset << "\n";
    std::cout << "[SnareDrum] RMS tail  (~192–245ms): " << rms_tail  << "\n";
    std::cout << "[SnareDrum] Ratio onset/tail:        " << ratio     << "\n";

    EXPECT_GT(rms_onset, 0.001f) << "No signal at onset";
    EXPECT_GT(ratio, 4.0f)
        << "Expected onset/tail ratio ≥ 4 (decay=150ms); got " << ratio;
}

// ---------------------------------------------------------------------------
// Test 3: BandpassShape — centroid between HPF (180 Hz) and LP (3500 Hz)
//
// The LP+HPF cascade creates a bandpass. Flat white noise centroid would be
// at Nyquist/2. After LP at 3500 Hz + HPF at 180 Hz, energy concentrates
// between 180–3500 Hz — centroid should fall in that band.
// ---------------------------------------------------------------------------

TEST_F(SnareDrumPatchTest, BandpassShape) {
    PRINT_TEST_HEADER(
        "Snare Drum — Bandpass Shape (automated)",
        "LP (3500 Hz) + HPF (180 Hz) creates a bandpass: centroid should be "
        "between 180 Hz and 3500 Hz.",
        "note_on → capture 2048 samples → spectral_centroid in [180, 3500] Hz",
        "180 Hz < centroid < 3500 Hz",
        sample_rate
    );

    ASSERT_EQ(engine_load_patch(engine(), kPatch), 0);
    engine_note_on(engine(), 60, 1.0f);

    const size_t BLOCK  = 512;
    const size_t WINDOW = 2048;
    std::vector<float> buf(BLOCK * 2);
    std::vector<float> mono;
    mono.reserve(WINDOW);

    for (int b = 0; b < 5; ++b) {
        engine_process(engine(), buf.data(), BLOCK);
        if (b >= 1) {
            for (size_t i = 0; i < BLOCK && mono.size() < WINDOW; ++i)
                mono.push_back(buf[i * 2]);
        }
    }
    engine_note_off(engine(), 60);

    if (mono.size() < WINDOW) mono.resize(WINDOW, 0.0f);

    float centroid = spectral_centroid(mono, sample_rate);
    std::cout << "[SnareDrum] Spectral centroid: " << centroid << " Hz\n";

    EXPECT_GT(centroid, 180.0f)
        << "Centroid below HPF cutoff — HPF may not be active";
    EXPECT_LT(centroid, 3500.0f)
        << "Centroid above LP cutoff — LP filter may not be shaping noise";
}

// ---------------------------------------------------------------------------
// Test 4: Audible — 4/4 pattern with snare on beats 2 and 4 at 90 BPM
// ---------------------------------------------------------------------------

TEST_F(SnareDrumPatchTest, TwoFourPatternAudible) {
    PRINT_TEST_HEADER(
        "Snare Drum — 2-and-4 Pattern at 90 BPM (audible)",
        "Snare hits on beats 2 and 4 of a 4/4 bar at 90 BPM. "
        "Hear the bandpass noise burst with fast decay characteristic of snare drum.",
        "engine_load_patch(snare_drum.json) → engine_start → 2×4 hits",
        "Audible mid-frequency noise snap with ~150ms decay.",
        sample_rate
    );

    ASSERT_EQ(engine_load_patch(engine(), kPatch), 0);
    ASSERT_ENGINE_START(engine());

    // 90 BPM: quarter note = 667ms. Play 2 bars: hits on beats 2 and 4.
    constexpr int BEAT_MS = 667;
    constexpr int GATE_MS = 15;

    std::cout << "[SnareDrum] Playing 2-and-4 pattern (2 bars at 90 BPM)…\n";
    for (int bar = 0; bar < 2; ++bar) {
        // Beat 1: silence
        std::this_thread::sleep_for(std::chrono::milliseconds(BEAT_MS));
        // Beat 2: snare
        engine_note_on(engine(), 60, 0.9f);
        std::this_thread::sleep_for(std::chrono::milliseconds(GATE_MS));
        engine_note_off(engine(), 60);
        std::this_thread::sleep_for(std::chrono::milliseconds(BEAT_MS - GATE_MS));
        // Beat 3: silence
        std::this_thread::sleep_for(std::chrono::milliseconds(BEAT_MS));
        // Beat 4: snare
        engine_note_on(engine(), 60, 0.9f);
        std::this_thread::sleep_for(std::chrono::milliseconds(GATE_MS));
        engine_note_off(engine(), 60);
        std::this_thread::sleep_for(std::chrono::milliseconds(BEAT_MS - GATE_MS));
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(400));
    engine_stop(engine());
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
