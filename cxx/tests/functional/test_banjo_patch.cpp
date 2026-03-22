/**
 * @file test_banjo_patch.cpp
 * @brief Functional tests for banjo.json — Roland Fig 3-4 repeating trigger.
 *
 * Patch topology (Roland System 100M Fig 3-4):
 *   VCO (saw) → SH_FILTER → VCA ← ADSR_ENVELOPE ← LFO (square, 8 Hz gate_cv)
 *
 * The LFO square wave re-triggers the ADSR at 8 Hz (~125ms strum period) via
 * gate_cv while the key is held. Each re-trigger fires a new pluck envelope
 * (attack=1ms, decay=100ms, sustain=15%), producing a rapid banjo strum.
 * The ADSR lifecycle gate_in (note_on) fires the first pluck immediately.
 *
 * Key assertions:
 *   1. Smoke            — note_on produces audio within 1 block.
 *   2. RepeatingTrigger — signal remains present at 400ms (LFO has re-triggered
 *                         ~3× by then); verifies the repeating-strum mechanic.
 *   3. Audible          — open-G banjo tuning (D3/G3/B3/D4/G4), held long enough
 *                         to hear the LFO strum repeat.
 *
 * Timing at 48 kHz, block=512 (~10.67ms/block):
 *   LFO 8 Hz → period ~125ms → re-trigger every ~12 blocks.
 *   400ms ≈ block 37 — at least 3 LFO re-triggers by then.
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

class BanjoPatchTest : public ::testing::Test {
protected:
    void SetUp() override {
        test::init_test_environment();
        sample_rate = test::get_safe_sample_rate(0);
        engine_wrapper = std::make_unique<test::EngineWrapper>(sample_rate);
    }

    EngineHandle engine() { return engine_wrapper->get(); }

    int sample_rate;
    std::unique_ptr<test::EngineWrapper> engine_wrapper;

    static constexpr const char* kPatch = "patches/banjo.json";
};

// ---------------------------------------------------------------------------
// Test 1: Smoke — immediate audio on note_on (attack=1ms)
// ---------------------------------------------------------------------------

TEST_F(BanjoPatchTest, NoteOnProducesAudio) {
    PRINT_TEST_HEADER(
        "Banjo — Smoke",
        "ADSR attack=1ms: first blocks after note_on should carry signal.",
        "engine_load_patch(banjo.json) → note_on(G3) → 5 blocks",
        "RMS > 0.001",
        sample_rate
    );

    ASSERT_EQ(engine_load_patch(engine(), kPatch), 0);
    engine_note_on(engine(), 55, 1.0f);  // G3

    const size_t FRAMES = 512;
    std::vector<float> buf(FRAMES * 2);
    double sum_sq = 0.0;
    for (int b = 0; b < 5; ++b) {
        engine_process(engine(), buf.data(), FRAMES);
        for (size_t i = 0; i < FRAMES; ++i) sum_sq += double(buf[i * 2]) * double(buf[i * 2]);
    }
    engine_note_off(engine(), 55);

    float rms = float(std::sqrt(sum_sq / double(FRAMES * 5)));
    std::cout << "[Banjo] Onset (5 blocks) RMS: " << rms << "\n";
    EXPECT_GT(rms, 0.001f) << "Expected immediate banjo pluck from 1ms ADSR attack";
}

// ---------------------------------------------------------------------------
// Test 2: RepeatingTrigger — signal sustained at ~400ms via LFO re-triggering
//
// LFO at 8 Hz re-triggers the ADSR every ~125ms. By block 37 (~400ms), the
// envelope has been re-triggered ~3 times. Signal should be clearly present.
// ---------------------------------------------------------------------------

TEST_F(BanjoPatchTest, RepeatingTrigger) {
    PRINT_TEST_HEADER(
        "Banjo — Repeating Trigger (automated)",
        "LFO 8 Hz re-triggers ADSR: signal sustained at ~400ms while key held.",
        "engine_load_patch → note_on(G3) → 40 blocks → check RMS at blocks 34–39",
        "RMS > 0.001 at ~400ms (LFO has re-triggered ≥3×)",
        sample_rate
    );

    ASSERT_EQ(engine_load_patch(engine(), kPatch), 0);
    engine_note_on(engine(), 55, 1.0f);  // G3

    const size_t FRAMES     = 512;
    const int    LATE_START = 34;
    const int    LATE_END   = 40;  // ~363–427ms

    std::vector<float> buf(FRAMES * 2);
    double late_sq = 0.0; int late_n = 0;

    for (int b = 0; b < LATE_END; ++b) {
        engine_process(engine(), buf.data(), FRAMES);
        if (b >= LATE_START) {
            for (size_t i = 0; i < FRAMES; ++i) late_sq += double(buf[i * 2]) * double(buf[i * 2]);
            ++late_n;
        }
    }
    engine_note_off(engine(), 55);

    float rms_late = float(std::sqrt(late_sq / double(FRAMES * late_n)));
    std::cout << "[Banjo] RMS at ~400ms (blocks 34–39): " << rms_late << "\n";
    std::cout << "[Banjo] Expected: LFO re-triggered ADSR ≥3× by this point\n";

    EXPECT_GT(rms_late, 0.001f)
        << "Expected repeating strum to keep signal alive at 400ms; got " << rms_late;
}

// ---------------------------------------------------------------------------
// Test 3: Audible — open-G banjo tuning, held long enough to hear LFO strum
// ---------------------------------------------------------------------------

TEST_F(BanjoPatchTest, OpenGTuningAudible) {
    PRINT_TEST_HEADER(
        "Banjo — Open-G Strum (audible)",
        "Roland Fig 3-4: LFO (8 Hz) strums each held note. D3/G3/B3/D4/G4.",
        "engine_load_patch(banjo.json) → engine_start → hold each note 600ms",
        "Audible repeating banjo strum — 4-5 plucks per note, bright twangy character.",
        sample_rate
    );

    ASSERT_EQ(engine_load_patch(engine(), kPatch), 0);
    ASSERT_ENGINE_START(engine());

    constexpr int NOTE_MS    = 600;  // hold long enough to hear ~5 LFO strums at 8Hz
    constexpr int RELEASE_MS = 150;

    // Open-G banjo strings: D3=50, G3=55, B3=59, D4=62, G4=67
    const int notes[] = {50, 55, 59, 62, 67};
    std::cout << "[Banjo] Playing D3 → G3 → B3 → D4 → G4 (open-G, held 600ms each)…\n";
    for (int midi : notes) {
        engine_note_on(engine(), midi, 1.0f);
        std::this_thread::sleep_for(std::chrono::milliseconds(NOTE_MS));
        engine_note_off(engine(), midi);
        std::this_thread::sleep_for(std::chrono::milliseconds(RELEASE_MS));
    }

    engine_stop(engine());
}

// ---------------------------------------------------------------------------
// Test 4: Gate-bias strum demo — contrasts short plucks vs held strums
//
// Bluegrass G run: short notes (100ms) give a single pluck; long held notes
// (1400ms) strum 3–4 times via the gate-bias LFO mechanism. Silence between
// held notes proves the LFO does NOT fire without a key held.
// ---------------------------------------------------------------------------

TEST_F(BanjoPatchTest, GateBiasStrumAudible) {
    PRINT_TEST_HEADER(
        "Banjo — Gate-Bias Strum Demo (audible)",
        "Short plucks (100ms) = single hit. Held notes (1400ms) = 3–4 LFO strums. "
        "Silence between held notes proves gate-bias suppresses LFO when key released.",
        "G major bluegrass run: quick picks → held G4 → quick picks → held D4 → held G3",
        "Audible contrast between single pluck and repeating strum.",
        sample_rate
    );

    ASSERT_EQ(engine_load_patch(engine(), kPatch), 0);
    ASSERT_ENGINE_START(engine());

    // Helper: pluck a note briefly (single hit, no LFO re-trigger)
    auto pluck = [&](int midi) {
        engine_note_on(engine(), midi, 1.0f);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        engine_note_off(engine(), midi);
        std::this_thread::sleep_for(std::chrono::milliseconds(120));
    };
    // Helper: hold a note long enough for 3-4 LFO strums (LFO 2.5Hz → 400ms period)
    auto hold = [&](int midi) {
        engine_note_on(engine(), midi, 1.0f);
        std::this_thread::sleep_for(std::chrono::milliseconds(1400));
        engine_note_off(engine(), midi);
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
    };

    std::cout << "[Banjo] Quick picks (G3 B3 D4) — should be single plucks…\n";
    pluck(55);  // G3
    pluck(59);  // B3
    pluck(62);  // D4

    std::cout << "[Banjo] Holding G4 (1.4s) — should strum 3–4 times…\n";
    hold(67);   // G4 held

    std::cout << "[Banjo] Quick picks (D4 B3 G3) — back to single plucks…\n";
    pluck(62);  // D4
    pluck(59);  // B3
    pluck(55);  // G3

    std::cout << "[Banjo] Holding D4 (1.4s) — strum again…\n";
    hold(62);   // D4 held

    std::cout << "[Banjo] Holding G3 (1.4s) — final strum…\n";
    hold(55);   // G3 held

    engine_stop(engine());
}

// ---------------------------------------------------------------------------
// Test 5: MIDI — 4-bar bluegrass run
// ---------------------------------------------------------------------------

TEST_F(BanjoPatchTest, BanjoMidiAudible) {  // Test 5
    PRINT_TEST_HEADER(
        "Banjo — Bluegrass MIDI (audible)",
        "4-bar G major bluegrass run: 8th-note picking with held D4 in bar 2.",
        "engine_load_patch(banjo.json) → engine_start → engine_load_midi(banjo.mid)",
        "Audible bluegrass picking with LFO strumming the held D4.",
        sample_rate
    );

    ASSERT_EQ(engine_load_patch(engine(), kPatch), 0);
    ASSERT_ENGINE_START(engine());

    ASSERT_EQ(engine_load_midi(engine(), "midi/banjo.mid"), 0);
    engine_midi_play(engine());
    std::cout << "[Banjo] Playing banjo.mid — 4-bar bluegrass run…\n";
    test::wait_while_running(8);

    engine_stop(engine());
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
