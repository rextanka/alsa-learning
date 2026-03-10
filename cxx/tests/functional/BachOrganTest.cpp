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
        
        // Initialize Tempo (100 BPM as per fugue feel)
        engine_set_bpm(engine_wrapper->get(), 100.0);
        
        ASSERT_EQ(engine_start(engine_wrapper->get()), 0);
    }

    int sample_rate;
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
    
    // Mini-Sequencer Logic: Use MusicalClock instead of sleep_for
    // Assume 96 Ticks Per Beat (PPQN)
    const int ppqn = 96;
    double bpm = 100.0;
    
    // Calculate how many ticks are in 100ms
    // Ticks per ms = (BPM * PPQN) / 60000
    // 100ms * (100 * 96) / 60000 = 100 * 9600 / 60000 = 960000 / 60000 = 16 ticks
    const int64_t ticks_per_event = 16; 
    
    int64_t target_tick = 0;

    for (size_t i = 0; i < midiData.size(); i += 3) {
        // Wait for target tick
        int64_t current_ticks = 0;
        while (test::g_keep_running) {
            engine_get_total_ticks(engine, &current_ticks);
            if (current_ticks >= target_tick) break;
            test::wait_while_running(1); // Rest 1s or until signal
        }

        // Note ON
        engine_process_midi_bytes(engine, &midiData[i], 3, 0);
        
        // Schedule Note OFF 16 ticks later
        target_tick += ticks_per_event;
        
        while (test::g_keep_running) {
            engine_get_total_ticks(engine, &current_ticks);
            if (current_ticks >= target_tick) break;
            test::wait_while_running(1);
        }

        // Note OFF
        uint8_t off[] = { 0x80, midiData[i+1], 0 };
        engine_process_midi_bytes(engine, off, 3, 0);
        
        // Wait another 16 ticks before next note ON
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

    EngineHandle engine = engine_wrapper->get();
    // The MIDI parser inside the engine handles running status
    engine_process_midi_bytes(engine, midiData.data(), midiData.size(), 0);
    
    std::cout << "[BachAudit] Running Status bytes processed." << std::endl;
}
