/**
 * @file test_wood_blocks_patch.cpp
 * @brief Functional tests for wood_blocks.json — resonant noise percussion.
 *
 * Patch topology (Practical Synthesis Vol.1 §3-3 resonant noise):
 *   WHITE_NOISE → SH_FILTER (cutoff=800 Hz, res=0.7)
 *       → AD_ENVELOPE (attack=1ms, decay=0.12s) → VCA
 *
 * By driving white noise through a highly resonant filter, the filter's
 * self-resonance peak creates a pitched quality from an unpitched source.
 * The VCF keyboard tracking (kybd_cv, auto-injected by Voice on note_on)
 * shifts the resonant peak in proportion to MIDI note number, so higher
 * keys produce a higher-pitched "knock".  The short AD envelope gives the
 * characteristic sharp transient of a struck wooden percussion instrument.
 *
 * Key assertions:
 *   1. Smoke          — note_on produces immediate audio (1ms attack).
 *   2. PercussiveDecay — onset RMS >> tail RMS (AD, no sustain).
 *   3. KeyboardTracking — spectral centroid of a high note (C6) is higher
 *                         than that of a low note (C3), confirming kybd_cv
 *                         shifts the resonant peak.
 *   4. Audible         — 4-note percussive figure (C3/G3/C4/G4).
 *
 * Timing at 48 kHz, block=512 (~10.67ms/block):
 *   decay=0.12s ≈ 11 blocks
 *   Onset: blocks 1–3   (~10–32ms, near peak)
 *   Tail:  blocks 18–22 (~192–235ms ≈ 1.5× decay constant)
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

class WoodBlocksPatchTest : public ::testing::Test {
protected:
    void SetUp() override {
        test::init_test_environment();
        sample_rate = test::get_safe_sample_rate(0);
        engine_wrapper = std::make_unique<test::EngineWrapper>(sample_rate);
    }

    EngineHandle engine() { return engine_wrapper->get(); }

    int sample_rate;
    std::unique_ptr<test::EngineWrapper> engine_wrapper;

    static constexpr const char* kPatch = "patches/wood_blocks.json";
};

// ---------------------------------------------------------------------------
// Test 1: Smoke — immediate audio on note_on (attack=1ms)
// ---------------------------------------------------------------------------

TEST_F(WoodBlocksPatchTest, NoteOnProducesAudio) {
    PRINT_TEST_HEADER(
        "Wood Blocks — Smoke",
        "AD_ENVELOPE attack=1ms: first blocks after note_on should carry signal.",
        "engine_load_patch(wood_blocks.json) → note_on(C4) → 4 blocks",
        "RMS > 0.001",
        sample_rate
    );

    ASSERT_EQ(engine_load_patch(engine(), kPatch), 0);
    engine_note_on(engine(), 60, 1.0f);  // C4

    const size_t FRAMES = 512;
    std::vector<float> buf(FRAMES * 2);
    double sum_sq = 0.0;
    for (int b = 0; b < 4; ++b) {
        engine_process(engine(), buf.data(), FRAMES);
        for (size_t i = 0; i < FRAMES; ++i) sum_sq += double(buf[i * 2]) * double(buf[i * 2]);
    }
    engine_note_off(engine(), 60);

    float rms = float(std::sqrt(sum_sq / double(FRAMES * 4)));
    std::cout << "[WoodBlocks] Onset (4 blocks) RMS: " << rms << "\n";
    EXPECT_GT(rms, 0.001f) << "Expected immediate knock from 1ms AD attack";
}

// ---------------------------------------------------------------------------
// Test 2: PercussiveDecay — onset RMS >> tail RMS
//
// onset: blocks 1–3   (~10–32ms, near peak)
// tail:  blocks 40–44 (~426–469ms, well past 0.25s decay constant)
// Expected: rms_onset / rms_tail ≥ 4.0
// ---------------------------------------------------------------------------

TEST_F(WoodBlocksPatchTest, PercussiveDecay) {
    PRINT_TEST_HEADER(
        "Wood Blocks — Percussive Decay (automated)",
        "AD_ENVELOPE decay=0.25s: onset RMS (~20ms) ≥ 4× tail RMS (~448ms).",
        "engine_load_patch → note_on(C4) → 44 blocks → compare onset vs tail RMS",
        "rms_onset / rms_tail ≥ 4.0",
        sample_rate
    );

    ASSERT_EQ(engine_load_patch(engine(), kPatch), 0);
    engine_note_on(engine(), 60, 1.0f);  // C4

    const size_t FRAMES      = 512;
    const int    ONSET_START = 1;
    const int    ONSET_END   = 4;   // ~10–43ms
    const int    TAIL_START  = 40;
    const int    TAIL_END    = 44;  // ~426–469ms

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
    // If the tail is completely silent (rms_tail < noise floor), the sound has
    // decayed to nothing — that is *more* percussive than ratio=4 requires.
    // Treat near-silence as effectively infinite ratio so the test passes.
    float ratio = (rms_tail > 1e-9f) ? rms_onset / rms_tail
                                      : (rms_onset > 0.001f ? 999.0f : 0.0f);

    std::cout << "[WoodBlocks] RMS onset (~10–43ms):   " << rms_onset << "\n";
    std::cout << "[WoodBlocks] RMS tail  (~426–469ms): " << rms_tail  << "\n";
    std::cout << "[WoodBlocks] Ratio onset/tail:        " << ratio     << "\n";

    EXPECT_GT(rms_onset, 0.001f) << "No signal at onset — check patch connectivity";
    EXPECT_GT(ratio, 4.0f)
        << "Expected onset/tail ratio ≥ 4 (AD decay=0.12s); got " << ratio;
}

// ---------------------------------------------------------------------------
// Test 3: KeyboardTracking — kybd_cv shifts resonant peak with note number
//
// The SH_FILTER's kybd_cv port is auto-injected by Voice on note_on.
// A high note (C6=72) should produce a brighter spectral centroid than a
// low note (C3=48) because the 1V/oct tracking shifts the resonant peak
// upward by 3 octaves (cutoff×8).
// Expected: centroid_high > centroid_low × 1.5
// ---------------------------------------------------------------------------

TEST_F(WoodBlocksPatchTest, KeyboardTracking) {
    PRINT_TEST_HEADER(
        "Wood Blocks — Keyboard Tracking (automated)",
        "kybd_cv auto-injection: C6 resonant peak higher than C3 resonant peak.",
        "note_on(C3) → capture centroid; note_on(C6) → capture centroid",
        "centroid_C6 > centroid_C3 × 1.5",
        sample_rate
    );

    const size_t FRAMES = 512;
    const size_t WINDOW = 2048;
    std::vector<float> buf(FRAMES * 2);

    auto capture_centroid = [&](int midi_note) -> float {
        EXPECT_EQ(engine_load_patch(engine(), kPatch), 0);
        engine_note_on(engine(), midi_note, 1.0f);

        std::vector<float> win;
        win.reserve(WINDOW);
        for (int b = 0; b < 8 && win.size() < WINDOW; ++b) {
            engine_process(engine(), buf.data(), FRAMES);
            if (b >= 1)
                for (size_t i = 0; i < FRAMES && win.size() < WINDOW; ++i)
                    win.push_back(buf[i * 2]);
        }
        engine_note_off(engine(), midi_note);
        if (win.size() < WINDOW) win.resize(WINDOW, 0.0f);
        return spectral_centroid(win, sample_rate);
    };

    float centroid_low  = capture_centroid(48);  // C3
    float centroid_high = capture_centroid(84);  // C6

    std::cout << "[WoodBlocks] Centroid C3 (low):  " << centroid_low  << " Hz\n";
    std::cout << "[WoodBlocks] Centroid C6 (high): " << centroid_high << " Hz\n";
    std::cout << "[WoodBlocks] Ratio high/low:     " << centroid_high / centroid_low << "\n";

    EXPECT_GT(centroid_high, centroid_low * 1.5f)
        << "Expected keyboard tracking to raise resonant peak for higher notes";
}

// ---------------------------------------------------------------------------
// Test 4: Audible — C3/G3/C4/G4 rhythmic figure
// ---------------------------------------------------------------------------

TEST_F(WoodBlocksPatchTest, RhythmicFigureAudible) {
    PRINT_TEST_HEADER(
        "Wood Blocks — Rhythmic Figure (audible)",
        "Pitched resonant-noise percussion across C3/G3/C4/G4.",
        "engine_load_patch(wood_blocks.json) → engine_start → C3/G3/C4/G4",
        "Audible sharp percussive knock with pitched quality from resonant SH_FILTER.",
        sample_rate
    );

    ASSERT_EQ(engine_load_patch(engine(), kPatch), 0);
    ASSERT_ENGINE_START(engine());

    constexpr int NOTE_MS    = 40;   // sharp strike
    constexpr int RELEASE_MS = 400;  // let 0.25s decay ring out

    const int notes[] = {48, 55, 60, 67};  // C3, G3, C4, G4
    std::cout << "[WoodBlocks] Playing C3 → G3 → C4 → G4…\n";
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
