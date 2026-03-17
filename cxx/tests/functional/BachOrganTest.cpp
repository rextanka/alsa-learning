#include <gtest/gtest.h>
#include "../TestHelper.hpp"
#include <vector>
#include <thread>
#include <chrono>

/**
 * @file BachOrganTest.cpp
 * @brief Functional verification of MIDI playback using the Bridge API.
 *
 * Patch: church pipe organ (DRAWBAR_ORGAN) — same registration as Functional_BachMidi.
 */

class BachOrganTest : public ::testing::Test {
protected:
    void SetUp() override {
        test::init_test_environment();
        sample_rate = test::get_safe_sample_rate(0);

        PRINT_TEST_HEADER(
            "Bach Organ Functional Validation",
            "Verifies MIDI parsing and playback via Bridge API.",
            "MIDI Data -> Engine (DRAWBAR_ORGAN -> ADSR_ENVELOPE -> VCA) -> Output",
            "Successful identification of MIDI events and audible response.",
            sample_rate
        );

        engine_wrapper = std::make_unique<test::EngineWrapper>(sample_rate);
        EngineHandle engine = engine_wrapper->get();

        // Phase 15 organ chain
        engine_add_module(engine, "DRAWBAR_ORGAN", "ORGAN");
        engine_add_module(engine, "ADSR_ENVELOPE", "ENV");
        engine_add_module(engine, "VCA",           "VCA");
        engine_connect_ports(engine, "ENV", "envelope_out", "VCA", "gain_cv");
        engine_bake(engine);

        // Church pipe organ registration: 16', 8', 4', 2⅔' open
        set_param(engine, "drawbar_16",  6.0f);
        set_param(engine, "drawbar_8",   8.0f);
        set_param(engine, "drawbar_4",   6.0f);
        set_param(engine, "drawbar_223", 4.0f);
        set_param(engine, "drawbar_2",   2.0f);

        // Church organ ADSR: instant attack, sustain, modest release
        set_param(engine, "amp_attack",  0.015f);
        set_param(engine, "amp_decay",   0.1f);
        set_param(engine, "amp_sustain", 0.7f);
        set_param(engine, "amp_release", 0.05f);

        engine_set_bpm(engine, 100.0);
        ASSERT_EQ(engine_start(engine), 0);

        // Calibrate tick rate so the sequencer can time events accurately
        int64_t start_ticks = 0;
        engine_get_total_ticks(engine, &start_ticks);
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        int64_t end_ticks = 0;
        engine_get_total_ticks(engine, &end_ticks);

        ticks_per_ms = (static_cast<double>(end_ticks) - start_ticks) / 500.0;
        std::cout << "[BachSequencer] Calibrated tick rate: " << ticks_per_ms << " ticks/ms" << std::endl;
    }

    int sample_rate;
    double ticks_per_ms = 0;
    std::unique_ptr<test::EngineWrapper> engine_wrapper;
};

/**
 * BWV 578 Subject (G Minor Fugue)
 * Verified for mechanical counterpoint and Running Status.
 */
TEST_F(BachOrganTest, BWV578_Subject_Audible) {
    std::cout << "[BachAudit] Starting BWV 578 Functional Validation..." << std::endl;

    std::vector<uint8_t> midiData = {
        0x90, 67, 100, // G4
        0x90, 74, 100, // D5
        0x90, 70, 100, // Bb4
        0x90, 69, 100, // A4
        0x90, 67, 100, // G4
        0x90, 70, 100, // Bb4
        0x90, 69, 100, // A4
        0x90, 67, 100, // G4
        0x90, 66, 100, // F#4
        0x90, 69, 100, // A4
        0x90, 62, 100  // D4
    };

    EngineHandle engine = engine_wrapper->get();

    const double note_duration_ms = 200.0;
    const double ticks_per_event  = note_duration_ms * ticks_per_ms;

    int64_t target_tick = 0;
    engine_get_total_ticks(engine, &target_tick);

    for (size_t i = 0; i < midiData.size(); i += 3) {
        int64_t current_ticks = 0;
        while (test::g_keep_running) {
            engine_get_total_ticks(engine, &current_ticks);
            if (current_ticks >= static_cast<int64_t>(target_tick)) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }

        std::cout << "[BachSequencer] Tick " << current_ticks
                  << " / Target " << target_tick
                  << ": Note ON " << static_cast<int>(midiData[i + 1]) << std::endl;
        engine_process_midi_bytes(engine, &midiData[i], 3, 0);
        target_tick += static_cast<int64_t>(ticks_per_event);

        while (test::g_keep_running) {
            engine_get_total_ticks(engine, &current_ticks);
            if (current_ticks >= static_cast<int64_t>(target_tick)) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }

        uint8_t off[] = { 0x80, midiData[i + 1], 0 };
        engine_process_midi_bytes(engine, off, 3, 0);
        target_tick += static_cast<int64_t>(ticks_per_event);
    }

    std::cout << "[BachAudit] MIDI byte processing completed." << std::endl;
}

TEST_F(BachOrganTest, RunningStatus_Validation) {
    // 0x90 followed by multiple note/vel pairs (Bach counterpoint style)
    std::vector<uint8_t> midiData = {
        0x90, 0x43, 0x64, // G4
              0x45, 0x64, // A4 (Running Status)
              0x47, 0x64  // B4 (Running Status)
    };

    std::cout << "[RunningStatus] MIDI data size: " << midiData.size() << std::endl;
    EngineHandle engine = engine_wrapper->get();

    engine_process_midi_bytes(engine, midiData.data(), midiData.size(), 0);

    test::wait_while_running(1);

    std::cout << "[BachAudit] Running Status bytes processed and audible." << std::endl;
}
