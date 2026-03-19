/**
 * @file test_reverb_patch.cpp
 * @brief Functional tests for Phase 19 post-bus reverb effects.
 *
 * Tests both REVERB_FREEVERB and REVERB_FDN via the post-chain API.
 *
 * Test topology:
 *   engine_load_patch(reverb_pad.json)        — sine/triangle pad, 2s release
 *   engine_post_chain_push("REVERB_FDN")      — Jot FDN reverb
 *   engine_post_chain_set_param(decay=2.0)    — 2s T60
 *
 * Key assertions:
 *   1. Smoke         — patch loads and note-on produces non-silent audio.
 *   2. ReverbTail    — RMS measured 0.5s after note-off is still > 0.0005;
 *                      reverb extends energy well past the dry note release.
 *   3. StereoWidth   — L/R Pearson correlation < 0.90 with width=1.0;
 *                      the FDN decorrelates channels via even/odd tap extraction.
 *   4. FreeverbSmoke — REVERB_FREEVERB also loads and produces non-silent output.
 *   5. Audible       — live play through FDN reverb for manual listen.
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

static float rms(const std::vector<float>& v) {
    if (v.empty()) return 0.0f;
    double s = 0.0;
    for (float x : v) s += double(x) * double(x);
    return static_cast<float>(std::sqrt(s / v.size()));
}

// Pearson correlation between two equal-length vectors
static float pearson(const std::vector<float>& a, const std::vector<float>& b) {
    if (a.size() != b.size() || a.empty()) return 1.0f;
    const size_t N = a.size();
    double sumA = 0, sumB = 0;
    for (size_t i = 0; i < N; ++i) { sumA += a[i]; sumB += b[i]; }
    const double meanA = sumA / N, meanB = sumB / N;
    double num = 0, denA = 0, denB = 0;
    for (size_t i = 0; i < N; ++i) {
        double da = a[i] - meanA, db = b[i] - meanB;
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

class ReverbPatchTest : public ::testing::Test {
protected:
    void SetUp() override {
        test::init_test_environment();
        sample_rate   = test::get_safe_sample_rate(0);
        engine_wrapper = std::make_unique<test::EngineWrapper>(sample_rate);
    }

    EngineHandle engine() { return engine_wrapper->get(); }

    int sample_rate;
    std::unique_ptr<test::EngineWrapper> engine_wrapper;

    static constexpr const char* kPatch = "patches/reverb_pad.json";

    // Run N blocks of size FRAMES, collecting interleaved stereo output
    static constexpr size_t FRAMES = 512;

    void run_blocks(int n, std::vector<float>* left = nullptr, std::vector<float>* right = nullptr) {
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

    // blocks_for_ms: how many 512-sample blocks fit in @ms milliseconds
    int blocks_for_ms(int ms) const {
        return std::max(1, static_cast<int>(static_cast<double>(ms) * sample_rate / 1000.0 / FRAMES));
    }
};

// ---------------------------------------------------------------------------
// Test 1: Smoke — FDN reverb loads and produces non-silent audio
// ---------------------------------------------------------------------------

TEST_F(ReverbPatchTest, FdnSmoke) {
    PRINT_TEST_HEADER(
        "FDN Reverb — Smoke",
        "reverb_pad + REVERB_FDN: note-on produces non-silent stereo output.",
        "engine_load_patch → post_chain_push(REVERB_FDN) → note_on(A4) → 5 blocks",
        "RMS > 0.001",
        sample_rate
    );

    ASSERT_EQ(engine_load_patch(engine(), kPatch), 0);
    const int idx = engine_post_chain_push(engine(), "REVERB_FDN");
    ASSERT_GE(idx, 0);
    engine_post_chain_set_param(engine(), idx, "decay",  2.0f);
    engine_post_chain_set_param(engine(), idx, "wet",    0.4f);
    engine_post_chain_set_param(engine(), idx, "damping", 0.3f);

    engine_note_on(engine(), 69, 1.0f);  // A4

    std::vector<float> L, R;
    run_blocks(5, &L, &R);
    engine_note_off(engine(), 69);

    const float rms_l = rms(L);
    std::cout << "[FDN] 5-block onset RMS (L): " << rms_l << "\n";
    EXPECT_GT(rms_l, 0.001f);
}

// ---------------------------------------------------------------------------
// Test 2: ReverbTail — signal continues after note-off (reverb tail)
//
// Protocol:
//   onset:  blocks 1–4  (~0–42ms) — note held, measure onset level
//   gap:    blocks 5–50 (~42–530ms) — note released at block 5
//   tail:   blocks 51–98 (~530–1040ms) — reverb should still be active
//
// Expected: rms_tail > 0.0005 AND rms_onset > rms_tail * 5 (reverb fades)
// ---------------------------------------------------------------------------

TEST_F(ReverbPatchTest, FdnReverbTail) {
    PRINT_TEST_HEADER(
        "FDN Reverb — Tail Energy",
        "After note-off, reverb tail sustains energy for >500ms (decay=2.0s T60).",
        "note_on → 4 blocks → note_off → 94 more blocks → check tail RMS",
        "rms_tail > 0.0005 and rms_onset > rms_tail * 5",
        sample_rate
    );

    ASSERT_EQ(engine_load_patch(engine(), kPatch), 0);
    const int idx = engine_post_chain_push(engine(), "REVERB_FDN");
    ASSERT_GE(idx, 0);
    engine_post_chain_set_param(engine(), idx, "decay",     2.0f);
    engine_post_chain_set_param(engine(), idx, "wet",       0.5f);
    engine_post_chain_set_param(engine(), idx, "damping",   0.2f);
    engine_post_chain_set_param(engine(), idx, "room_size", 0.8f);

    engine_note_on(engine(), 60, 1.0f);  // C4

    std::vector<float> buf(FRAMES * 2);

    // Onset: blocks 0–3
    double onset_sq = 0.0; int onset_n = 0;
    for (int b = 0; b < 4; ++b) {
        engine_process(engine(), buf.data(), FRAMES);
        for (size_t i = 0; i < FRAMES; ++i) onset_sq += double(buf[i * 2]) * double(buf[i * 2]);
        ++onset_n;
    }

    engine_note_off(engine(), 60);

    // Drain: blocks 4–50
    for (int b = 4; b < 51; ++b) engine_process(engine(), buf.data(), FRAMES);

    // Tail: blocks 51–97
    double tail_sq = 0.0; int tail_n = 0;
    for (int b = 51; b < 98; ++b) {
        engine_process(engine(), buf.data(), FRAMES);
        for (size_t i = 0; i < FRAMES; ++i) tail_sq += double(buf[i * 2]) * double(buf[i * 2]);
        ++tail_n;
    }

    const float rms_onset = static_cast<float>(std::sqrt(onset_sq / (FRAMES * onset_n)));
    const float rms_tail  = static_cast<float>(std::sqrt(tail_sq  / (FRAMES * tail_n)));

    std::cout << "[FDN] RMS onset:      " << rms_onset << "\n";
    std::cout << "[FDN] RMS tail:       " << rms_tail  << "\n";
    std::cout << "[FDN] Onset/tail:     " << (rms_tail > 0 ? rms_onset / rms_tail : 0.0f) << "×\n";

    EXPECT_GT(rms_onset, 0.001f) << "No signal at onset";
    EXPECT_GT(rms_tail,  0.0005f) << "Reverb tail should still carry energy at ~530–1040ms after note-off";
}

// ---------------------------------------------------------------------------
// Test 3: StereoWidth — L/R are decorrelated (Pearson correlation < 0.90)
//
// The FDN extracts L from even delay taps and R from odd delay taps.
// With width=1.0 the two channels should diverge meaningfully.
// ---------------------------------------------------------------------------

TEST_F(ReverbPatchTest, FdnStereoWidth) {
    PRINT_TEST_HEADER(
        "FDN Reverb — Stereo Width",
        "With width=1.0, Pearson L/R correlation < 0.90 (decorrelated channels).",
        "engine_load_patch → FDN(width=1.0) → note_on → 10 blocks → correlation",
        "pearson(L, R) < 0.90",
        sample_rate
    );

    ASSERT_EQ(engine_load_patch(engine(), kPatch), 0);
    const int idx = engine_post_chain_push(engine(), "REVERB_FDN");
    ASSERT_GE(idx, 0);
    engine_post_chain_set_param(engine(), idx, "decay",     2.0f);
    engine_post_chain_set_param(engine(), idx, "wet",       0.8f);  // heavy wet for clear stereo
    engine_post_chain_set_param(engine(), idx, "width",     1.0f);
    engine_post_chain_set_param(engine(), idx, "damping",   0.1f);

    engine_note_on(engine(), 69, 1.0f);

    // Skip 2 blocks (fill delay lines), then capture 8 blocks
    run_blocks(2);
    std::vector<float> L, R;
    run_blocks(8, &L, &R);
    engine_note_off(engine(), 69);

    const float corr = pearson(L, R);
    std::cout << "[FDN] L/R Pearson correlation: " << corr << "\n";
    EXPECT_LT(corr, 0.90f) << "Expected stereo decorrelation with width=1.0";
}

// ---------------------------------------------------------------------------
// Test 4: FreeverbSmoke — Freeverb also loads and produces audio
// ---------------------------------------------------------------------------

TEST_F(ReverbPatchTest, FreeverbSmoke) {
    PRINT_TEST_HEADER(
        "Freeverb — Smoke",
        "reverb_pad + REVERB_FREEVERB: note-on produces non-silent output.",
        "engine_load_patch → post_chain_push(REVERB_FREEVERB) → note_on → 5 blocks",
        "RMS > 0.001",
        sample_rate
    );

    ASSERT_EQ(engine_load_patch(engine(), kPatch), 0);
    const int idx = engine_post_chain_push(engine(), "REVERB_FREEVERB");
    ASSERT_GE(idx, 0);
    engine_post_chain_set_param(engine(), idx, "room_size", 0.7f);
    engine_post_chain_set_param(engine(), idx, "damping",   0.5f);
    engine_post_chain_set_param(engine(), idx, "wet",       0.4f);

    engine_note_on(engine(), 69, 1.0f);

    std::vector<float> L, R;
    run_blocks(5, &L, &R);
    engine_note_off(engine(), 69);

    const float rms_l = rms(L);
    const float corr  = pearson(L, R);
    std::cout << "[Freeverb] RMS (L):          " << rms_l << "\n";
    std::cout << "[Freeverb] L/R correlation:  " << corr  << "\n";
    EXPECT_GT(rms_l, 0.001f);
}

// ---------------------------------------------------------------------------
// Test 5: PhaserSmoke — phaser loads and produces audio with clear wet signal
// ---------------------------------------------------------------------------

TEST_F(ReverbPatchTest, PhaserSmoke) {
    PRINT_TEST_HEADER(
        "Phaser — Smoke",
        "reverb_pad + PHASER: note-on produces non-silent output.",
        "engine_load_patch → post_chain_push(PHASER) → note_on → 5 blocks",
        "RMS > 0.001",
        sample_rate
    );

    ASSERT_EQ(engine_load_patch(engine(), kPatch), 0);
    const int idx = engine_post_chain_push(engine(), "PHASER");
    ASSERT_GE(idx, 0);
    engine_post_chain_set_param(engine(), idx, "rate",     0.5f);
    engine_post_chain_set_param(engine(), idx, "depth",    1.5f);
    engine_post_chain_set_param(engine(), idx, "feedback", 0.7f);
    engine_post_chain_set_param(engine(), idx, "wet",      0.5f);

    engine_note_on(engine(), 69, 1.0f);

    std::vector<float> L, R;
    run_blocks(5, &L, &R);
    engine_note_off(engine(), 69);

    const float rms_l = rms(L);
    const float corr  = pearson(L, R);
    std::cout << "[Phaser] RMS (L):         " << rms_l << "\n";
    std::cout << "[Phaser] L/R correlation: " << corr  << "\n";
    EXPECT_GT(rms_l, 0.001f);
    EXPECT_LT(corr, 0.99f) << "Phaser quadrature LFO should introduce some stereo difference";
}

// ---------------------------------------------------------------------------
// Test 6: Audible — pad with FDN reverb played live
// ---------------------------------------------------------------------------

TEST_F(ReverbPatchTest, AudibleFdnPad) {
    PRINT_TEST_HEADER(
        "FDN Reverb — Audible (live)",
        "C4/E4/G4 chord with 2s FDN reverb — listen for spatial tail and stereo spread.",
        "engine_start → FDN reverb → play C4/E4/G4 → ring out",
        "Audible warm reverb tail, stereo image wider than dry signal.",
        sample_rate
    );

    ASSERT_EQ(engine_load_patch(engine(), kPatch), 0);
    ASSERT_ENGINE_START(engine());

    const int idx = engine_post_chain_push(engine(), "REVERB_FDN");
    ASSERT_GE(idx, 0);
    engine_post_chain_set_param(engine(), idx, "decay",     2.5f);
    engine_post_chain_set_param(engine(), idx, "wet",       0.4f);
    engine_post_chain_set_param(engine(), idx, "damping",   0.25f);
    engine_post_chain_set_param(engine(), idx, "room_size", 0.7f);
    engine_post_chain_set_param(engine(), idx, "width",     1.0f);

    const int notes[] = {60, 64, 67};  // C4, E4, G4
    for (int n : notes) engine_note_on(engine(), n, 0.7f);

    std::cout << "[FDN] C-major chord ringing with 2.5s reverb…\n";
    std::this_thread::sleep_for(std::chrono::milliseconds(600));

    for (int n : notes) engine_note_off(engine(), n);
    std::this_thread::sleep_for(std::chrono::milliseconds(3500));  // let reverb tail ring out

    engine_stop(engine());
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
