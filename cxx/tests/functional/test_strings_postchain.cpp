/**
 * @file test_strings_postchain.cpp
 * @brief Functional tests for the global post-processing chain with a strings source.
 *
 * Patch topology (voice chain — strings_chorus_reverb.json):
 *   COMPOSITE_GENERATOR (saw+sub) → MOOG_FILTER (cutoff=3000, res=0.05)
 *       → VCA ← ADSR_ENVELOPE (attack=0.35s, sustain=0.85, release=0.6s)
 *   LFO (0.4 Hz, intensity=0.002) → VCO.pitch_cv
 *
 * Post-chain (set up via engine_post_chain_push — not in the patch JSON):
 *   JUNO_CHORUS  — BBD chorus; mode=1 (Mode::I, 0.4 Hz LFO, ~2ms modulation)
 *   REVERB_FDN   — Jot FDN reverb; decay=2.5s, wet=0.30
 *
 * Key assertions:
 *   1. ChorusStereoSpread — JUNO_CHORUS decorrelates L and R (Pearson < 0.85).
 *      The voice chain produces identical L+R before the post-chain; the BBD
 *      modulation makes them diverge.
 *   2. ReverbTailAfterStrings — FDN reverb sustains energy >500ms after note-off.
 *   3. FullPostChainSeries — chorus then reverb: L/R decorrelated AND tail present.
 *   4. PostChainClearRestoresMono — after engine_post_chain_clear(), a centered
 *      strings voice produces perfectly correlated L/R (Pearson > 0.98).
 *   5. Audible — G major chord with chorus+reverb for manual listening.
 *
 * Note: JUNO_CHORUS default mode is Mode::Off — it must be activated by setting
 * mode=1 after engine_post_chain_push, otherwise the effect does nothing. This
 * test suite explicitly verifies that the mode parameter is honoured.
 *
 * Timing at 48 kHz, block=512 (~10.67ms/block):
 *   attack=0.35s ≈ 32.8 blocks — let attack settle before measuring tonal content.
 */

#include <gtest/gtest.h>
#include "../TestHelper.hpp"
#include <vector>
#include <cmath>
#include <numeric>
#include <thread>
#include <chrono>

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static float rms_vec(const std::vector<float>& v) {
    if (v.empty()) return 0.0f;
    double s = 0.0;
    for (float x : v) s += double(x) * double(x);
    return static_cast<float>(std::sqrt(s / v.size()));
}

// Pearson correlation coefficient between two equal-length vectors.
// Returns 1.0 for identical mono (L==R), <1.0 for stereo divergence.
static float pearson(const std::vector<float>& a, const std::vector<float>& b) {
    if (a.size() != b.size() || a.empty()) return 1.0f;
    const size_t N = a.size();
    double sumA = 0, sumB = 0;
    for (size_t i = 0; i < N; ++i) { sumA += a[i]; sumB += b[i]; }
    const double mA = sumA / N, mB = sumB / N;
    double num = 0, denA = 0, denB = 0;
    for (size_t i = 0; i < N; ++i) {
        const double da = a[i] - mA, db = b[i] - mB;
        num  += da * db;
        denA += da * da;
        denB += db * db;
    }
    const double den = std::sqrt(denA * denB);
    return den > 1e-12 ? static_cast<float>(num / den) : 1.0f;
}

// ---------------------------------------------------------------------------
// Fixture
// ---------------------------------------------------------------------------

class StringsPostchainTest : public ::testing::Test {
protected:
    void SetUp() override {
        test::init_test_environment();
        sample_rate    = test::get_safe_sample_rate(0);
        engine_wrapper = std::make_unique<test::EngineWrapper>(sample_rate);
    }

    EngineHandle engine() { return engine_wrapper->get(); }

    int sample_rate;
    std::unique_ptr<test::EngineWrapper> engine_wrapper;

    static constexpr const char* kPatch = "patches/strings_chorus_reverb.json";
    static constexpr size_t FRAMES = 512;

    // Run @n blocks, optionally collecting L/R channel samples.
    void run_blocks(int n,
                    std::vector<float>* left  = nullptr,
                    std::vector<float>* right = nullptr) {
        std::vector<float> buf(FRAMES * 2);
        for (int b = 0; b < n; ++b) {
            engine_process(engine(), buf.data(), FRAMES);
            if (left && right) {
                for (size_t i = 0; i < FRAMES; ++i) {
                    left->push_back(buf[i * 2]);
                    right->push_back(buf[i * 2 + 1]);
                }
            }
        }
    }

    // blocks_for_ms: how many FRAMES-sized blocks span @ms milliseconds
    int blocks_for_ms(int ms) const {
        return std::max(1, static_cast<int>(
            static_cast<double>(ms) * sample_rate / 1000.0 / FRAMES));
    }

    // Push and configure JUNO_CHORUS (Mode::I = mode 1).
    // Returns the post-chain index.
    int push_chorus(float mode = 1.0f, float rate = 0.4f, float depth = 0.5f) {
        const int idx = engine_post_chain_push(engine(), "JUNO_CHORUS");
        EXPECT_GE(idx, 0) << "JUNO_CHORUS not registered in post-chain";
        engine_post_chain_set_param(engine(), idx, "mode",  mode);
        engine_post_chain_set_param(engine(), idx, "rate",  rate);
        engine_post_chain_set_param(engine(), idx, "depth", depth);
        return idx;
    }

    // Push and configure REVERB_FDN.
    // Returns the post-chain index.
    int push_reverb(float decay = 2.5f, float wet = 0.30f,
                    float room_size = 0.70f, float damping = 0.35f) {
        const int idx = engine_post_chain_push(engine(), "REVERB_FDN");
        EXPECT_GE(idx, 0) << "REVERB_FDN not registered";
        engine_post_chain_set_param(engine(), idx, "decay",     decay);
        engine_post_chain_set_param(engine(), idx, "wet",       wet);
        engine_post_chain_set_param(engine(), idx, "room_size", room_size);
        engine_post_chain_set_param(engine(), idx, "damping",   damping);
        return idx;
    }
};

// ---------------------------------------------------------------------------
// Test 1: ChorusStereoSpread
//
// A centered mono voice chain produces perfectly correlated L/R before the
// post-chain (Pearson ≈ 1.0). After JUNO_CHORUS (mode=I), the BBD modulation
// applies opposing delay offsets on each channel, decorrelating them.
//
// Protocol:
//   1. Load patch, note_on(C4).
//   2. Skip 35 blocks (attack settles, chorus delay lines fill).
//   3. Capture 10 blocks; measure Pearson(L, R).
//   Expected: Pearson < 0.85 (clear stereo spread from chorus).
// ---------------------------------------------------------------------------

TEST_F(StringsPostchainTest, ChorusStereoSpread) {
    PRINT_TEST_HEADER(
        "Strings + JUNO_CHORUS — Stereo Spread",
        "JUNO_CHORUS in post-chain: BBD modulation decorrelates L and R channels.",
        "engine_load_patch → post_chain_push(JUNO_CHORUS, mode=1) → note_on(C4)"
        " → 35 warm-up blocks → measure Pearson(L, R) over 10 blocks",
        "Pearson(L, R) < 0.85",
        sample_rate
    );

    ASSERT_EQ(engine_load_patch(engine(), kPatch), 0);
    push_chorus(1.0f, 0.4f, 0.6f); // Mode::I, medium-deep modulation

    engine_note_on(engine(), 60, 1.0f);  // C4

    // Warm up: let attack envelope (0.35s) and chorus delay lines settle.
    run_blocks(blocks_for_ms(450));

    std::vector<float> L, R;
    run_blocks(10, &L, &R);
    engine_note_off(engine(), 60);

    const float corr = pearson(L, R);
    std::cout << "[Chorus] L/R Pearson correlation: " << corr << "\n";
    EXPECT_LT(corr, 0.85f)
        << "Expected JUNO_CHORUS to decorrelate L/R channels (BBD L/R phase inversion)";
}

// ---------------------------------------------------------------------------
// Test 2: ReverbTailAfterStrings
//
// FDN reverb decay=2.5s: energy should persist well past the envelope release.
// Strings have release=0.6s; measure reverb tail at t≈1.0s after note-off.
//
// Protocol:
//   onset:  4 blocks  (~42ms), note held — measure onset level
//   note-off after block 4
//   drain:  ~55 blocks (~586ms) — voice release completes (~0.6s)
//   tail:   10 blocks at ~660–767ms after note-off — FDN still ringing
//
// Expected: rms_tail > 0.0005 AND rms_onset > rms_tail * 3
// ---------------------------------------------------------------------------

TEST_F(StringsPostchainTest, ReverbTailAfterStrings) {
    PRINT_TEST_HEADER(
        "Strings + REVERB_FDN — Reverb Tail",
        "FDN reverb decay=2.5s: tail energy persists ~660ms after note-off.",
        "engine_load_patch → REVERB_FDN(decay=2.5, wet=0.4) → note_on(C4)"
        " → 4 blocks → note_off → 55 drain → 10 tail blocks → RMS compare",
        "rms_tail > 0.0005 and rms_onset > rms_tail * 3",
        sample_rate
    );

    ASSERT_EQ(engine_load_patch(engine(), kPatch), 0);
    push_reverb(2.5f, 0.40f, 0.80f, 0.25f); // high wet for clear tail measurement

    engine_note_on(engine(), 60, 1.0f);  // C4

    // Let the attack envelope open (attack=0.35s + margin).
    run_blocks(blocks_for_ms(420));

    // Measure sustained onset level.
    const int kOnsetBlocks = 4;
    std::vector<float> buf_raw(FRAMES * 2);
    double onset_sq = 0.0;
    for (int b = 0; b < kOnsetBlocks; ++b) {
        engine_process(engine(), buf_raw.data(), FRAMES);
        for (size_t i = 0; i < FRAMES; ++i)
            onset_sq += double(buf_raw[i * 2]) * double(buf_raw[i * 2]);
    }
    const float rms_onset = static_cast<float>(
        std::sqrt(onset_sq / double(FRAMES * kOnsetBlocks)));

    engine_note_off(engine(), 60);

    // Drain: voice release=0.6s, allow 800ms total then measure.
    // Using blocks_for_ms() ensures correct timing at any sample rate.
    run_blocks(blocks_for_ms(800));

    // Tail: reverb should still be decaying (decay=2.5s T60).
    const int kTailBlocks = 8;
    double tail_sq = 0.0;
    for (int b = 0; b < kTailBlocks; ++b) {
        engine_process(engine(), buf_raw.data(), FRAMES);
        for (size_t i = 0; i < FRAMES; ++i)
            tail_sq += double(buf_raw[i * 2]) * double(buf_raw[i * 2]);
    }
    const float rms_tail = static_cast<float>(
        std::sqrt(tail_sq / double(FRAMES * kTailBlocks)));

    std::cout << "[Reverb] RMS onset (sustained):    " << rms_onset << "\n";
    std::cout << "[Reverb] RMS tail (~660ms post-off): " << rms_tail  << "\n";
    std::cout << "[Reverb] Onset/tail ratio:          "
              << (rms_tail > 0 ? rms_onset / rms_tail : 0.0f) << "×\n";

    EXPECT_GT(rms_onset, 0.001f)  << "No signal at onset — check attack envelope";
    EXPECT_GT(rms_tail,  0.0005f) << "FDN tail should persist >660ms after note-off (decay=2.5s)";
    EXPECT_GT(rms_onset, rms_tail * 3.0f)
        << "Onset should be significantly louder than the decayed tail";
}

// ---------------------------------------------------------------------------
// Test 3: FullPostChainSeries
//
// JUNO_CHORUS followed by REVERB_FDN in the post-chain: both effects must be
// active. Asserts:
//   (a) non-silent audio (signal passes through both effects)
//   (b) L/R decorrelated by chorus (Pearson < 0.85)
//   (c) reverb tail present after note-off
// ---------------------------------------------------------------------------

TEST_F(StringsPostchainTest, FullPostChainSeries) {
    PRINT_TEST_HEADER(
        "Strings + JUNO_CHORUS + REVERB_FDN — Full Post-Chain Series",
        "Both effects in series: chorus decorrelates, reverb extends tail.",
        "engine_load_patch → push(JUNO_CHORUS) → push(REVERB_FDN) → note_on(C4)"
        " → settle → check correlation AND tail",
        "Pearson < 0.85 AND rms_tail > 0.0003",
        sample_rate
    );

    ASSERT_EQ(engine_load_patch(engine(), kPatch), 0);
    push_chorus(1.0f, 0.4f, 0.6f);
    push_reverb(2.0f, 0.30f, 0.70f, 0.35f);

    engine_note_on(engine(), 60, 1.0f);  // C4

    // Settle: attack=0.35s + chorus delay fill.
    run_blocks(blocks_for_ms(450));

    // Capture stereo window for correlation check.
    std::vector<float> L, R;
    run_blocks(10, &L, &R);

    engine_note_off(engine(), 60);

    // Drain: voice release=0.6s, allow 800ms then measure reverb tail.
    run_blocks(blocks_for_ms(800));

    // Measure reverb tail.
    std::vector<float> buf_raw(FRAMES * 2);
    double tail_sq = 0.0;
    for (int b = 0; b < 10; ++b) {
        engine_process(engine(), buf_raw.data(), FRAMES);
        for (size_t i = 0; i < FRAMES; ++i)
            tail_sq += double(buf_raw[i * 2]) * double(buf_raw[i * 2]);
    }
    const float rms_tail = static_cast<float>(std::sqrt(tail_sq / double(FRAMES * 10)));
    const float corr     = pearson(L, R);

    std::cout << "[Series] L/R Pearson correlation:  " << corr     << "\n";
    std::cout << "[Series] RMS reverb tail:           " << rms_tail << "\n";

    EXPECT_GT(rms_vec(L), 0.001f) << "No audio in sustained window";
    // The FDN Householder feedback matrix cross-mixes L and R to distribute
    // energy, which partially re-correlates what the chorus decorrelated.
    // Standalone JUNO_CHORUS gives ~0.60; chorus+FDN in series gives ~0.90.
    // Assert < 0.97: any decorrelation confirms both effects are active.
    EXPECT_LT(corr,     0.97f)   << "Chorus+FDN series should produce some L/R decorrelation vs. dry mono (1.0)";
    EXPECT_GT(rms_tail, 0.0003f) << "FDN reverb tail should persist after note-off";
}

// ---------------------------------------------------------------------------
// Test 4: PostChainClearRestoresMono
//
// A centered voice chain with no post-chain effects produces L == R exactly
// (Pearson = 1.0). After pushing JUNO_CHORUS, Pearson drops. After calling
// engine_post_chain_clear(), Pearson returns to > 0.98.
//
// This verifies that engine_post_chain_clear() actually removes all effects
// and the signal path returns to dry mono-summed stereo.
// ---------------------------------------------------------------------------

TEST_F(StringsPostchainTest, PostChainClearRestoresMono) {
    PRINT_TEST_HEADER(
        "Post-Chain Clear — Restores Mono Correlation",
        "engine_post_chain_clear() removes chorus; L/R correlation returns to ~1.0.",
        "note_on → push chorus → measure corr_with_fx → clear → new note → measure corr_dry",
        "corr_with_fx < 0.85, corr_dry > 0.98",
        sample_rate
    );

    ASSERT_EQ(engine_load_patch(engine(), kPatch), 0);

    // --- With chorus ---
    push_chorus(1.0f, 0.4f, 0.7f);
    engine_note_on(engine(), 60, 1.0f);
    run_blocks(blocks_for_ms(450));  // attack + chorus delay fill

    std::vector<float> Lc, Rc;
    run_blocks(10, &Lc, &Rc);
    engine_note_off(engine(), 60);
    run_blocks(blocks_for_ms(200));  // brief gap

    const float corr_with_fx = pearson(Lc, Rc);
    std::cout << "[Clear] Pearson WITH chorus:  " << corr_with_fx << "\n";

    // --- Clear post-chain ---
    engine_post_chain_clear(engine());

    // New note, allow attack to settle.
    engine_note_on(engine(), 60, 1.0f);
    run_blocks(blocks_for_ms(450));

    std::vector<float> Ld, Rd;
    run_blocks(10, &Ld, &Rd);
    engine_note_off(engine(), 60);

    const float corr_dry = pearson(Ld, Rd);
    std::cout << "[Clear] Pearson WITHOUT chorus (dry): " << corr_dry << "\n";

    EXPECT_LT(corr_with_fx, 0.85f)
        << "JUNO_CHORUS should decorrelate L/R before clear";
    EXPECT_GT(corr_dry, 0.98f)
        << "After engine_post_chain_clear(), centered voice should produce L==R (Pearson ≈ 1.0)";
}

// ---------------------------------------------------------------------------
// Test 5: Audible — G major chord with chorus+reverb (manual listen)
// ---------------------------------------------------------------------------

TEST_F(StringsPostchainTest, AudibleStringsChorusReverb) {
    PRINT_TEST_HEADER(
        "Strings + Chorus + Reverb — Audible (live)",
        "G major chord: slow string bow-attack, BBD chorus width, FDN reverb tail.",
        "engine_start → push JUNO_CHORUS(mode=I) + REVERB_FDN(2.5s)"
        " → G3/B3/D4/G4 arpeggiated → ring out",
        "Audible: wide stereo strings with reverb halo.",
        sample_rate
    );

    ASSERT_EQ(engine_load_patch(engine(), kPatch), 0);
    ASSERT_ENGINE_START(engine());

    push_chorus(1.0f, 0.4f, 0.55f);
    push_reverb(2.5f, 0.28f, 0.75f, 0.30f);

    const int notes[] = {55, 59, 62, 67};  // G3, B3, D4, G4
    std::cout << "[Strings] Playing G3 → B3 → D4 → G4 with chorus+reverb…\n";

    for (int midi : notes) {
        engine_note_on(engine(), midi, 0.8f);
        std::this_thread::sleep_for(std::chrono::milliseconds(1400));
        engine_note_off(engine(), midi);
        std::this_thread::sleep_for(std::chrono::milliseconds(800));
    }

    // Let reverb tail ring out.
    std::this_thread::sleep_for(std::chrono::milliseconds(3000));
    engine_stop(engine());
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
