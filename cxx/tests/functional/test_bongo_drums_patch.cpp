/**
 * @file test_bongo_drums_patch.cpp
 * @brief Functional tests for bongo_drums.json — tonal resonant percussion.
 *
 * Patch topology (Practical Synthesis Vol. 2, Fig 3-22 bongo approximation):
 *   COMPOSITE_GENERATOR (triangle) → SH_FILTER (cutoff=320 Hz, res=0.65)
 *       → VCA ← AD_ENVELOPE (attack=1ms, decay=180ms)
 *   AD_ENVELOPE → VCF.cutoff_cv  (envelope sweeps cutoff downward from peak)
 *
 * The triangle wave through a resonant LP filter creates a tonal pitch — the
 * filter resonance produces the characteristic bongo "bonk". Keyboard tracking
 * (kybd_cv auto-injected by Voice) shifts the resonance peak with note pitch,
 * allowing the bongo pitch to track across the keyboard. The ENV also modulates
 * the cutoff, adding an initial brightness transient that decays away.
 *
 * Key assertions:
 *   1. Smoke          — note_on produces non-silent audio.
 *   2. PercussiveDecay — onset RMS >> tail RMS (decay=180ms).
 *   3. TonalContent   — spectral centroid is detectable and keyboard-tracked:
 *                       centroid from C3 < centroid from C5 (higher note =
 *                       higher resonance peak = higher apparent pitch).
 *   4. Audible        — syncopated bongo rhythm at 100 BPM.
 *
 * Timing at 48 kHz, block=512 (~10.67ms/block):
 *   decay=180ms ≈ 16.9 blocks.
 *   ONSET: blocks 0–3   (~0–32ms)    — near peak
 *   TAIL:  blocks 24–28 (~256–299ms) — ≈ e^(−1.42) ≈ 24% of onset
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

class BongoDrumsPatchTest : public ::testing::Test {
protected:
    void SetUp() override {
        test::init_test_environment();
        sample_rate = test::get_safe_sample_rate(0);
        engine_wrapper = std::make_unique<test::EngineWrapper>(sample_rate);
    }

    EngineHandle engine() { return engine_wrapper->get(); }

    int sample_rate;
    std::unique_ptr<test::EngineWrapper> engine_wrapper;

    static constexpr const char* kPatch = "patches/bongo_drums.json";
};

// ---------------------------------------------------------------------------
// Test 1: Smoke — note-on produces non-silent audio
// ---------------------------------------------------------------------------

TEST_F(BongoDrumsPatchTest, NoteOnProducesAudio) {
    PRINT_TEST_HEADER(
        "Bongo Drums — Smoke",
        "Triangle VCO through resonant LP filter + AD envelope produces non-silent audio.",
        "engine_load_patch(bongo_drums.json) → note_on(C4) → engine_process × 5",
        "RMS > 0.001 within first 5 blocks.",
        sample_rate
    );

    ASSERT_EQ(engine_load_patch(engine(), kPatch), 0);
    engine_note_on(engine(), 60, 1.0f);  // C4

    const size_t FRAMES = 512;
    std::vector<float> buf(FRAMES * 2);
    double sum_sq = 0.0;
    for (int b = 0; b < 5; ++b) {
        engine_process(engine(), buf.data(), FRAMES);
        for (size_t i = 0; i < FRAMES; ++i) sum_sq += double(buf[i * 2]) * double(buf[i * 2]);
    }
    float rms = float(std::sqrt(sum_sq / double(FRAMES * 5)));
    std::cout << "[BongoDrums] Onset RMS: " << rms << "\n";
    EXPECT_GT(rms, 0.001f) << "Expected non-silent onset";

    engine_note_off(engine(), 60);
}

// ---------------------------------------------------------------------------
// Test 2: PercussiveDecay — onset RMS > tail RMS × 3 (decay=180ms)
// ---------------------------------------------------------------------------

TEST_F(BongoDrumsPatchTest, PercussiveDecay) {
    PRINT_TEST_HEADER(
        "Bongo Drums — Percussive Decay (automated)",
        "AD decay=180ms: onset RMS (~0–32ms) is ≥3× tail RMS (~256–299ms).",
        "engine_load_patch → note_on(C4) → 28 blocks → compare onset vs tail RMS",
        "rms_onset / rms_tail ≥ 3.0",
        sample_rate
    );

    ASSERT_EQ(engine_load_patch(engine(), kPatch), 0);
    engine_note_on(engine(), 60, 1.0f);

    const size_t FRAMES      = 512;
    const int    ONSET_START = 0;
    const int    ONSET_END   = 3;   // ~0–32ms
    const int    TAIL_START  = 24;
    const int    TAIL_END    = 28;  // ~256–299ms

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

    std::cout << "[BongoDrums] RMS onset (~0–32ms):    " << rms_onset << "\n";
    std::cout << "[BongoDrums] RMS tail  (~256–299ms): " << rms_tail  << "\n";
    std::cout << "[BongoDrums] Ratio onset/tail:        " << ratio     << "\n";

    EXPECT_GT(rms_onset, 0.001f) << "No signal at onset";
    EXPECT_GT(ratio, 3.0f)
        << "Expected onset/tail ratio ≥ 3 (decay=180ms); got " << ratio;
}

// ---------------------------------------------------------------------------
// Test 3: KeyboardTracking — C5 centroid > C3 centroid
//
// kybd_cv auto-injection shifts the SH_FILTER resonance peak upward for higher
// notes. C3 (130 Hz) vs C5 (523 Hz) should yield measurably different centroids.
// ---------------------------------------------------------------------------

TEST_F(BongoDrumsPatchTest, KeyboardTracking) {
    PRINT_TEST_HEADER(
        "Bongo Drums — Keyboard Tracking (automated)",
        "kybd_cv shifts VCF resonance peak: C5 should have higher centroid than C3.",
        "load → note_on(C3) → centroid_low; reload → note_on(C5) → centroid_high",
        "centroid_high > centroid_low × 1.5",
        sample_rate
    );

    const size_t BLOCK  = 512;
    const size_t WINDOW = 2048;
    std::vector<float> buf(BLOCK * 2);

    auto capture_centroid = [&](int midi_note) -> float {
        auto wrapper = std::make_unique<test::EngineWrapper>(sample_rate);
        EngineHandle eng = wrapper->get();
        EXPECT_EQ(engine_load_patch(eng, kPatch), 0);
        engine_note_on(eng, midi_note, 1.0f);

        std::vector<float> mono;
        mono.reserve(WINDOW);
        for (int b = 0; b < 5 && mono.size() < WINDOW; ++b) {
            engine_process(eng, buf.data(), BLOCK);
            for (size_t i = 0; i < BLOCK && mono.size() < WINDOW; ++i)
                mono.push_back(buf[i * 2]);
        }
        engine_note_off(eng, midi_note);
        if (mono.size() < WINDOW) mono.resize(WINDOW, 0.0f);
        return spectral_centroid(mono, sample_rate);
    };

    float centroid_low  = capture_centroid(48);  // C3
    float centroid_high = capture_centroid(72);  // C5

    std::cout << "[BongoDrums] Centroid C3: " << centroid_low  << " Hz\n";
    std::cout << "[BongoDrums] Centroid C5: " << centroid_high << " Hz\n";

    EXPECT_GT(centroid_high, centroid_low * 1.5f)
        << "Expected C5 centroid > 1.5× C3 centroid (keyboard tracking active)";
}

// ---------------------------------------------------------------------------
// Test 4: Audible — syncopated bongo pattern at 100 BPM
// ---------------------------------------------------------------------------

TEST_F(BongoDrumsPatchTest, SyncopatedPatternAudible) {
    PRINT_TEST_HEADER(
        "Bongo Drums — Syncopated Pattern at 100 BPM (audible)",
        "Bongo rhythm with high and low pitches across a syncopated clave-like pattern. "
        "Keyboard tracking shifts resonance pitch per note.",
        "engine_load_patch(bongo_drums.json) → engine_start → syncopated hits",
        "Audible tonal percussive bonk with pitch variation across the keyboard.",
        sample_rate
    );

    ASSERT_EQ(engine_load_patch(engine(), kPatch), 0);
    ASSERT_ENGINE_START(engine());

    // 100 BPM: quarter note = 600ms, eighth note = 300ms
    constexpr int EIGHTH_MS = 300;
    constexpr int GATE_MS   = 20;

    // Pattern: high(C5) . low(G3) . high . . low . (syncopated feel)
    const std::pair<int,int> pattern[] = {
        {72, EIGHTH_MS},  // C5 high bongo
        {0,  EIGHTH_MS},  // rest
        {55, EIGHTH_MS},  // G3 low bongo
        {0,  EIGHTH_MS},  // rest
        {72, EIGHTH_MS},  // C5
        {72, EIGHTH_MS},  // C5
        {55, EIGHTH_MS},  // G3
        {0,  EIGHTH_MS},  // rest
    };

    std::cout << "[BongoDrums] Playing syncopated pattern at 100 BPM…\n";
    for (auto& [note, dur] : pattern) {
        if (note > 0) {
            engine_note_on(engine(), note, 0.9f);
            std::this_thread::sleep_for(std::chrono::milliseconds(GATE_MS));
            engine_note_off(engine(), note);
            std::this_thread::sleep_for(std::chrono::milliseconds(dur - GATE_MS));
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(dur));
        }
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(400));
    engine_stop(engine());
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
