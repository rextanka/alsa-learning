/**
 * @file test_bass_drum_patch.cpp
 * @brief Functional tests for bass_drum.json — pitched percussive thud.
 *
 * Patch topology:
 *   COMPOSITE_GENERATOR (sine=0.7, triangle=0.3) → VCA ← AD_ENVELOPE (attack=2ms, decay=350ms)
 *
 * Sine + triangle at the note pitch (C2 = 65.4 Hz) give the characteristic low
 * thud of a kick drum. AD envelope with 350ms decay gives the boom-and-decay shape.
 *
 * Key assertions:
 *   1. Smoke         — note_on produces non-silent audio.
 *   2. PercussiveDecay — onset RMS >> tail RMS (decay=300ms, sustain=0).
 *   3. DeepSpectrum  — centroid < 800 Hz (Moog LP at 280 Hz concentrates
 *                      energy in the low-frequency range).
 *   4. Audible       — kick drum pattern at 90 BPM.
 *
 * Timing at 48 kHz, block=512 (~10.67ms/block):
 *   decay=300ms ≈ 28.1 blocks.
 *   ONSET: blocks 0–3   (~0–32ms)    — near peak
 *   TAIL:  blocks 38–44 (~405–469ms) — well into decay tail
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

class BassDrumPatchTest : public ::testing::Test {
protected:
    void SetUp() override {
        test::init_test_environment();
        sample_rate = test::get_safe_sample_rate(0);
        engine_wrapper = std::make_unique<test::EngineWrapper>(sample_rate);
    }

    EngineHandle engine() { return engine_wrapper->get(); }

    int sample_rate;
    std::unique_ptr<test::EngineWrapper> engine_wrapper;

    static constexpr const char* kPatch = "patches/bass_drum.json";
};

// ---------------------------------------------------------------------------
// Test 1: Smoke — note-on produces non-silent audio
// ---------------------------------------------------------------------------

TEST_F(BassDrumPatchTest, NoteOnProducesAudio) {
    PRINT_TEST_HEADER(
        "Bass Drum — Smoke",
        "VCO (sine+triangle at C2=65 Hz) + AD envelope produces non-silent audio.",
        "engine_load_patch(bass_drum.json) → note_on → engine_process × 5",
        "RMS > 0.001 within first 5 blocks.",
        sample_rate
    );

    ASSERT_EQ(engine_load_patch(engine(), kPatch), 0);
    engine_note_on(engine(), 36, 1.0f);  // C2 (typical kick pitch)

    const size_t FRAMES = 512;
    std::vector<float> buf(FRAMES * 2);
    double sum_sq = 0.0;
    for (int b = 0; b < 5; ++b) {
        engine_process(engine(), buf.data(), FRAMES);
        for (size_t i = 0; i < FRAMES; ++i) sum_sq += double(buf[i * 2]) * double(buf[i * 2]);
    }
    float rms = float(std::sqrt(sum_sq / double(FRAMES * 5)));
    std::cout << "[BassDrum] Onset RMS: " << rms << "\n";
    EXPECT_GT(rms, 0.001f) << "Expected non-silent onset from VCO kick";

    engine_note_off(engine(), 36);
}

// ---------------------------------------------------------------------------
// Test 2: PercussiveDecay — onset RMS > tail RMS × 4 (decay=300ms)
//
// At block 41 (~437ms), envelope ≈ e^(−437/300) ≈ 0.23 of peak.
// Onset at blocks 0–3 should be substantially louder than tail at blocks 38–44.
// ---------------------------------------------------------------------------

TEST_F(BassDrumPatchTest, PercussiveDecay) {
    PRINT_TEST_HEADER(
        "Bass Drum — Percussive Decay (automated)",
        "AD decay=350ms: onset RMS (~0–32ms) is ≥4× the tail RMS (~405–469ms).",
        "engine_load_patch → note_on → 44 blocks → compare onset vs tail RMS",
        "rms_onset / rms_tail ≥ 4.0",
        sample_rate
    );

    ASSERT_EQ(engine_load_patch(engine(), kPatch), 0);
    engine_note_on(engine(), 36, 1.0f);

    const size_t FRAMES      = 512;
    const int    ONSET_START = 0;
    const int    ONSET_END   = 3;   // ~0–32ms (peak)
    const int    TAIL_START  = 38;
    const int    TAIL_END    = 44;  // ~405–469ms (decaying)

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
    engine_note_off(engine(), 36);

    float rms_onset = float(std::sqrt(onset_sq / double(FRAMES * onset_n)));
    float rms_tail  = float(std::sqrt(tail_sq  / double(FRAMES * tail_n)));
    float ratio     = rms_tail > 1e-9f ? rms_onset / rms_tail : 0.0f;

    std::cout << "[BassDrum] RMS onset (~0–32ms):    " << rms_onset << "\n";
    std::cout << "[BassDrum] RMS tail  (~405–469ms): " << rms_tail  << "\n";
    std::cout << "[BassDrum] Ratio onset/tail:        " << ratio     << "\n";

    EXPECT_GT(rms_onset, 0.001f) << "No signal at onset";
    EXPECT_GT(ratio, 4.0f)
        << "Expected onset/tail ratio ≥ 4 (decay=300ms); got " << ratio;
}

// ---------------------------------------------------------------------------
// Test 3: DeepSpectrum — VCO at C2 (65 Hz) dominates the low-frequency range
//
// Sine + triangle at 65.4 Hz concentrates spectral energy well below 800 Hz.
// ---------------------------------------------------------------------------

TEST_F(BassDrumPatchTest, DeepSpectrum) {
    PRINT_TEST_HEADER(
        "Bass Drum — Deep Spectrum (automated)",
        "VCO sine+triangle at C2 (65 Hz) concentrates spectral energy in the low range. "
        "Centroid should be well below 800 Hz.",
        "note_on → capture 2048 onset samples → spectral_centroid < 800 Hz",
        "centroid < 800 Hz",
        sample_rate
    );

    ASSERT_EQ(engine_load_patch(engine(), kPatch), 0);
    engine_note_on(engine(), 36, 1.0f);

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
    engine_note_off(engine(), 36);

    if (mono.size() < WINDOW) mono.resize(WINDOW, 0.0f);

    float centroid = spectral_centroid(mono, sample_rate);
    std::cout << "[BassDrum] Spectral centroid: " << centroid << " Hz\n";
    EXPECT_LT(centroid, 800.0f)
        << "Centroid " << centroid << " Hz is too high — "
        << "VCO at C2 (65 Hz) should concentrate energy in the deep bass range";
}

// ---------------------------------------------------------------------------
// Test 4: Audible — kick drum pattern at 90 BPM (beats 1 and 3)
// ---------------------------------------------------------------------------

TEST_F(BassDrumPatchTest, KickPatternAudible) {
    PRINT_TEST_HEADER(
        "Bass Drum — Kick Pattern at 90 BPM (audible)",
        "Kick drum on beats 1 and 3 of 4/4 at 90 BPM. "
        "VCO sine+triangle kick with 350ms decay boom.",
        "engine_load_patch(bass_drum.json) → engine_start → 2 bars",
        "Audible deep kick thud on every other beat.",
        sample_rate
    );

    ASSERT_EQ(engine_load_patch(engine(), kPatch), 0);
    ASSERT_ENGINE_START(engine());

    constexpr int BEAT_MS = 667;  // 90 BPM
    constexpr int GATE_MS = 15;

    std::cout << "[BassDrum] Playing kick on beats 1 and 3 (2 bars at 90 BPM)…\n";
    for (int bar = 0; bar < 2; ++bar) {
        // Beat 1: kick
        engine_note_on(engine(), 36, 1.0f);
        std::this_thread::sleep_for(std::chrono::milliseconds(GATE_MS));
        engine_note_off(engine(), 36);
        std::this_thread::sleep_for(std::chrono::milliseconds(BEAT_MS - GATE_MS));
        // Beat 2: silence
        std::this_thread::sleep_for(std::chrono::milliseconds(BEAT_MS));
        // Beat 3: kick
        engine_note_on(engine(), 36, 1.0f);
        std::this_thread::sleep_for(std::chrono::milliseconds(GATE_MS));
        engine_note_off(engine(), 36);
        std::this_thread::sleep_for(std::chrono::milliseconds(BEAT_MS - GATE_MS));
        // Beat 4: silence
        std::this_thread::sleep_for(std::chrono::milliseconds(BEAT_MS));
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(600));
    engine_stop(engine());
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
