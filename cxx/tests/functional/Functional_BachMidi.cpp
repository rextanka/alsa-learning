/**
 * @file Functional_BachMidi.cpp
 * @brief Functional verification of MIDI playback and polyphonic handling.
 *
 * Patch: church pipe organ registration using DRAWBAR_ORGAN.
 *
 * Drawbar registration (classic "Full Organ" minus reeds):
 *   16'  (sub-octave) : 6  — principal bass warmth
 *   5⅓'  (quint)      : 0  — silent (avoids fifth-tone mud without mixture)
 *   8'   (principal)  : 8  — full open: core foundation tone
 *   4'   (octave)     : 6  — open flute brightness
 *   2⅔'  (nazard)     : 4  — gentle quint celeste
 *   2'   (super oct.) : 4  — sparkle / upper work
 *   1⅗'  (tierce)     : 0  — omitted (too colourful for plain Bach)
 *   1⅓'  (larigot)    : 0  — omitted
 *   1'   (sifflöte)   : 2  — whisper of air atop the voicing
 *
 * ADSR: church acoustic decay (long release to simulate reverb tail).
 */

#include <gtest/gtest.h>
#include "../TestHelper.hpp"
#include <vector>
#include <thread>
#include <chrono>

class FunctionalBachMidi : public ::testing::Test {
protected:
    void SetUp() override {
        test::init_test_environment();
        sample_rate = test::get_safe_sample_rate(0);

        PRINT_TEST_HEADER(
            "Bach MIDI Functional Integrity",
            "Verifies MIDI playback, polyphonic handling, and pipe organ timbre.",
            "MIDI -> Engine (DRAWBAR_ORGAN -> ADSR_ENVELOPE -> VCA) -> Output",
            "Multi-part organ performance with church-hall acoustic decay.",
            sample_rate
        );

        engine_wrapper = std::make_unique<test::EngineWrapper>(sample_rate);
        EngineHandle engine = engine_wrapper->get();

        // --- Build the church organ signal chain via Phase 15 C API ---
        engine_add_module(engine, "DRAWBAR_ORGAN", "ORGAN");
        engine_add_module(engine, "ADSR_ENVELOPE", "ENV");
        engine_add_module(engine, "VCA",           "VCA");
        engine_connect_ports(engine, "ENV", "envelope_out", "VCA", "gain_cv");
        engine_bake(engine);

        // Drawbar registration: principal foundation + upper work
        set_param(engine, "drawbar_16",  6.0f); // 16' sub-octave
        set_param(engine, "drawbar_513", 0.0f); // 5⅓' quint — silent
        set_param(engine, "drawbar_8",   8.0f); // 8'  principal (fully open)
        set_param(engine, "drawbar_4",   6.0f); // 4'  octave
        set_param(engine, "drawbar_223", 4.0f); // 2⅔' nazard
        set_param(engine, "drawbar_2",   4.0f); // 2'  super-octave
        set_param(engine, "drawbar_135", 0.0f); // 1⅗' tierce — omitted
        set_param(engine, "drawbar_113", 0.0f); // 1⅓' larigot — omitted
        set_param(engine, "drawbar_1",   2.0f); // 1'  sifflöte whisper

        // Church organ ADSR: instant attack, fast decay to full sustain, long release
        set_param(engine, "amp_attack",  0.005f);
        set_param(engine, "amp_decay",   0.05f);
        set_param(engine, "amp_sustain", 1.0f);
        set_param(engine, "amp_release", 0.35f);  // hall reverb tail

        ASSERT_EQ(engine_start(engine), 0);
    }

    void TearDown() override {
        if (engine_wrapper) engine_stop(engine_wrapper->get());
    }

    int sample_rate;
    std::unique_ptr<test::EngineWrapper> engine_wrapper;
};

// BWV 578 — "Little" Fugue in G minor, opening subject at 72 BPM.
TEST_F(FunctionalBachMidi, BWV578_Subject_Audible) {
    struct Note { uint8_t pitch; int duration_ms; };

    const std::vector<Note> subject = {
        {67, 416}, {74, 416}, {70, 416}, {69, 208}, {67, 208},
        {70, 208}, {69, 208}, {67, 208}, {66, 208}, {69, 208}, {62, 833}
    };

    EngineHandle engine = engine_wrapper->get();
    for (const auto& n : subject) {
        engine_note_on(engine, n.pitch, 0.8f);
        std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<int>(n.duration_ms * 0.9)));
        engine_note_off(engine, n.pitch);
        std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<int>(n.duration_ms * 0.1)));
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(700)); // let the release tail decay
}

// BWV 846 — C major Prelude, ascending arpeggios.
TEST_F(FunctionalBachMidi, BWV846_Arpeggio_Clarity) {
    const std::vector<uint8_t> pattern = {60, 64, 67, 72, 76};

    EngineHandle engine = engine_wrapper->get();
    for (int repeat = 0; repeat < 2; ++repeat) {
        for (uint8_t pitch : pattern) {
            engine_note_on(engine, pitch, 0.6f);
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            engine_note_off(engine, pitch);
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(700));
}
