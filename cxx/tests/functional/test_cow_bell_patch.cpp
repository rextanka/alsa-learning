/**
 * @file test_cow_bell_patch.cpp
 * @brief Functional tests for cow_bell.json — cascaded-filter metallic percussion.
 *
 * Patch topology (Practical Synthesis Vol. 2, Fig 3-15 cow bell approximation):
 *   COMPOSITE_GENERATOR (pulse, 50% width, +12 semitones up)
 *       → SH_FILTER (LP, cutoff=4800 Hz, res=0.75)
 *       → SH_FILTER (LP, cutoff=2200 Hz, res=0.55)
 *       → VCA ← AD_ENVELOPE (attack=1ms, decay=400ms)
 *
 * Two cascaded LP filters create the distinctive metallic resonance of a
 * cowbell. The high-resonance first filter creates a bright spectral peak;
 * the second filter rounds off the harsh top end while preserving the metallic
 * character. The pulse wave provides a dense harmonic series for both filters
 * to shape. transpose=+12 raises the VCO one octave for the higher register
 * typical of cowbell.
 *
 * Key assertions:
 *   1. Smoke         — note_on produces non-silent audio.
 *   2. PercussiveDecay — onset RMS >> tail RMS (decay=400ms).
 *   3. HighCentroid  — dual LP filters still pass energy above 1000 Hz;
 *                      centroid > 1000 Hz (metallic frequency range).
 *   4. Audible       — classic cowbell 4-on-the-floor pattern at 120 BPM.
 *
 * Timing at 48 kHz, block=512 (~10.67ms/block):
 *   decay=400ms ≈ 37.5 blocks.
 *   ONSET: blocks 0–3   (~0–32ms)
 *   TAIL:  blocks 50–55 (~533–587ms)
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

class CowBellPatchTest : public ::testing::Test {
protected:
    void SetUp() override {
        test::init_test_environment();
        sample_rate = test::get_safe_sample_rate(0);
        engine_wrapper = std::make_unique<test::EngineWrapper>(sample_rate);
    }

    EngineHandle engine() { return engine_wrapper->get(); }

    int sample_rate;
    std::unique_ptr<test::EngineWrapper> engine_wrapper;

    static constexpr const char* kPatch = "patches/cow_bell.json";
};

// ---------------------------------------------------------------------------
// Test 1: Smoke — produces non-silent audio
// ---------------------------------------------------------------------------

TEST_F(CowBellPatchTest, NoteOnProducesAudio) {
    PRINT_TEST_HEADER(
        "Cow Bell — Smoke",
        "Pulse VCO through dual resonant LP filters + AD envelope produces non-silent audio.",
        "engine_load_patch(cow_bell.json) → note_on(A5) → engine_process × 5",
        "RMS > 0.001 within first 5 blocks.",
        sample_rate
    );

    ASSERT_EQ(engine_load_patch(engine(), kPatch), 0);
    engine_note_on(engine(), 81, 1.0f);  // A5 (typical cowbell pitch)

    const size_t FRAMES = 512;
    std::vector<float> buf(FRAMES * 2);
    double sum_sq = 0.0;
    for (int b = 0; b < 5; ++b) {
        engine_process(engine(), buf.data(), FRAMES);
        for (size_t i = 0; i < FRAMES; ++i) sum_sq += double(buf[i * 2]) * double(buf[i * 2]);
    }
    float rms = float(std::sqrt(sum_sq / double(FRAMES * 5)));
    std::cout << "[CowBell] Onset RMS: " << rms << "\n";
    EXPECT_GT(rms, 0.001f) << "Expected non-silent onset from pulse+dual-LP chain";

    engine_note_off(engine(), 81);
}

// ---------------------------------------------------------------------------
// Test 2: PercussiveDecay — onset RMS > tail RMS × 3 (decay=400ms)
//
// At t=550ms (block 52), envelope ≈ e^(−550/400) ≈ 0.255 of peak.
// Onset/tail ratio ≥ 3 is conservative given the exponential decay.
// ---------------------------------------------------------------------------

TEST_F(CowBellPatchTest, PercussiveDecay) {
    PRINT_TEST_HEADER(
        "Cow Bell — Percussive Decay (automated)",
        "AD decay=400ms: onset RMS (~0–32ms) is ≥3× tail RMS (~533–587ms).",
        "engine_load_patch → note_on(A5) → 55 blocks → compare onset vs tail RMS",
        "rms_onset / rms_tail ≥ 3.0",
        sample_rate
    );

    ASSERT_EQ(engine_load_patch(engine(), kPatch), 0);
    engine_note_on(engine(), 81, 1.0f);

    const size_t FRAMES      = 512;
    const int    ONSET_START = 0;
    const int    ONSET_END   = 3;   // ~0–32ms
    const int    TAIL_START  = 50;
    const int    TAIL_END    = 55;  // ~533–587ms

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
    engine_note_off(engine(), 81);

    float rms_onset = float(std::sqrt(onset_sq / double(FRAMES * onset_n)));
    float rms_tail  = float(std::sqrt(tail_sq  / double(FRAMES * tail_n)));
    float ratio     = rms_tail > 1e-9f ? rms_onset / rms_tail : 0.0f;

    std::cout << "[CowBell] RMS onset (~0–32ms):    " << rms_onset << "\n";
    std::cout << "[CowBell] RMS tail  (~533–587ms): " << rms_tail  << "\n";
    std::cout << "[CowBell] Ratio onset/tail:        " << ratio     << "\n";

    EXPECT_GT(rms_onset, 0.001f) << "No signal at onset";
    EXPECT_GT(ratio, 3.0f)
        << "Expected onset/tail ratio ≥ 3 (decay=400ms); got " << ratio;
}

// ---------------------------------------------------------------------------
// Test 3: HighCentroid — dual LP filters pass metallic frequency content
//
// Despite two LP filters, the resonance peaks at 4800 Hz and 2200 Hz
// concentrate spectral energy in the 1–3 kHz metallic range.
// The pulse wave's harmonic series (from the high-pitched VCO) provides
// plenty of content for the filters to work with.
// Expected: centroid > 1000 Hz.
// ---------------------------------------------------------------------------

TEST_F(CowBellPatchTest, HighCentroid) {
    PRINT_TEST_HEADER(
        "Cow Bell — Metallic Spectrum (automated)",
        "Dual resonant LP filters (4800 Hz + 2200 Hz) shape pulse wave: "
        "centroid should be in the metallic range (> 1000 Hz).",
        "note_on(A5) → capture 2048 onset samples → spectral_centroid > 1000 Hz",
        "centroid > 1000 Hz (metallic resonance range)",
        sample_rate
    );

    ASSERT_EQ(engine_load_patch(engine(), kPatch), 0);
    engine_note_on(engine(), 81, 1.0f);  // A5

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
    engine_note_off(engine(), 81);

    if (mono.size() < WINDOW) mono.resize(WINDOW, 0.0f);

    float centroid = spectral_centroid(mono, sample_rate);
    std::cout << "[CowBell] Spectral centroid: " << centroid << " Hz\n";

    EXPECT_GT(centroid, 1000.0f)
        << "Expected centroid > 1000 Hz (metallic range from dual resonant LP filters)";
}

// ---------------------------------------------------------------------------
// Test 4: Audible — classic cowbell 4-on-the-floor at 120 BPM
// ---------------------------------------------------------------------------

TEST_F(CowBellPatchTest, FourOnTheFloorAudible) {
    PRINT_TEST_HEADER(
        "Cow Bell — 4-on-the-Floor at 120 BPM (audible)",
        "Classic cowbell on every beat at 120 BPM. "
        "Hear the metallic resonant decay from the dual-LP filtered pulse wave.",
        "engine_load_patch(cow_bell.json) → engine_start → 8 beats at 120 BPM",
        "Audible metallic cowbell 'clank' with 400ms decay.",
        sample_rate
    );

    ASSERT_EQ(engine_load_patch(engine(), kPatch), 0);
    ASSERT_ENGINE_START(engine());

    constexpr int BEAT_MS = 500;  // 120 BPM
    constexpr int GATE_MS = 20;

    std::cout << "[CowBell] Playing 4-on-the-floor (2 bars at 120 BPM)…\n";
    for (int i = 0; i < 8; ++i) {
        engine_note_on(engine(), 81, 0.9f);
        std::this_thread::sleep_for(std::chrono::milliseconds(GATE_MS));
        engine_note_off(engine(), 81);
        std::this_thread::sleep_for(std::chrono::milliseconds(BEAT_MS - GATE_MS));
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(600));
    engine_stop(engine());
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
