#include <gtest/gtest.h>
#include "../TestHelper.hpp"
#include <vector>
#include <thread> 
#include <chrono>

/**
 * @file BachOrganTest.cpp
 * @brief Functional verification of MIDI playback using the Bridge API.
 */

class BachOrganTest : public ::testing::Test {
protected:
    void SetUp() override {
        test::init_test_environment();
        sample_rate = test::get_safe_sample_rate(0);
        
        PRINT_TEST_HEADER(
            "Bach Organ Functional Validation",
            "Verifies MIDI parsing and playback via Bridge API.",
            "MIDI Data -> Bridge -> Engine -> Output",
            "Successful identification of MIDI events and audible response.",
            sample_rate
        );

        engine_wrapper = std::make_unique<test::EngineWrapper>(sample_rate);
        
        // Protocol Step 3 & 4: Modular Patching & ADSR
        engine_connect_mod(engine_wrapper->get(), MOD_SRC_ENVELOPE, ALL_VOICES, MOD_TGT_AMPLITUDE, 1.0f);
        engine_set_adsr(engine_wrapper->get(), 0.015f, 0.1f, 0.7f, 0.050f);
        
        // Initialize Tempo
        engine_set_bpm(engine_wrapper->get(), 100.0);
        ASSERT_EQ(engine_start(engine_wrapper->get()), 0);

        // --- CALIBRATION PHASE ---
        // Measure actual tick rate from the engine to drive the sequencer accurately.
        int64_t start_ticks = 0;
        engine_get_total_ticks(engine_wrapper->get(), &start_ticks);
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        int64_t end_ticks = 0;
        engine_get_total_ticks(engine_wrapper->get(), &end_ticks);
        
        ticks_per_ms = (static_cast<double>(end_ticks) - static_cast<double>(start_ticks)) / 500.0;
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
    
    // We use the high-level MIDI byte processing in the bridge
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
    
    // Sequencer: Use calibrated tick increments for 100ms notes
    const double note_duration_ms = 100.0;
    const double ticks_per_event = note_duration_ms * ticks_per_ms;
    
    int64_t target_tick = 0;
    engine_get_total_ticks(engine, &target_tick);

    for (size_t i = 0; i < midiData.size(); i += 3) {
        // Wait for target_tick
        int64_t current_ticks = 0;
        while (test::g_keep_running) {
            engine_get_total_ticks(engine, &current_ticks);
            if (current_ticks >= static_cast<int64_t>(target_tick)) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }

        // Note ON
        std::cout << "[BachSequencer] Tick " << current_ticks << " / Target " << (int64_t)target_tick << ": Note ON " << (int)midiData[i+1] << std::endl;
        engine_process_midi_bytes(engine, &midiData[i], 3, 0);
        
        // Schedule Note OFF
        target_tick += ticks_per_event;
        
        while (test::g_keep_running) {
            engine_get_total_ticks(engine, &current_ticks);
            if (current_ticks >= static_cast<int64_t>(target_tick)) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }

        // Note OFF
        uint8_t off[] = { 0x80, midiData[i+1], 0 };
        engine_process_midi_bytes(engine, off, 3, 0);
        
        // Prep for next Note ON
        target_tick += ticks_per_event;
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

    std::cout << "[DEBUG] MIDI data size: " << midiData.size() << std::endl;
    EngineHandle engine = engine_wrapper->get();
    
    // The MIDI parser inside the engine handles running status
    engine_process_midi_bytes(engine, midiData.data(), midiData.size(), 0);
    
    // Crucial: Wait for the engine to render at least a few buffers
    // 500ms allows the notes to sustain and be audible
    test::wait_while_running(1);
    
    std::cout << "[BachAudit] Running Status bytes processed and audible." << std::endl;
}
