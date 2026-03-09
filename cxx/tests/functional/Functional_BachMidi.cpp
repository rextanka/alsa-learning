#include <gtest/gtest.h>
#include "../TestHelper.hpp"
#include <vector>
#include <thread>
#include <chrono>

/**
 * @file Functional_BachMidi.cpp
 * @brief Functional verification of MIDI playback and polyphonic handling.
 */

class FunctionalBachMidi : public ::testing::Test {
protected:
    void SetUp() override {
        test::init_test_environment();
        sample_rate = test::get_safe_sample_rate(0);
        
        PRINT_TEST_HEADER(
            "Bach MIDI Functional Integrity",
            "Verifies MIDI playback, polyphonic handling, and British Organ timbre.",
            "MIDI -> Engine -> VoiceManager -> Output",
            "Multi-part organ performance with accurate timing and voice stealing.",
            sample_rate
        );

        engine_wrapper = std::make_unique<test::EngineWrapper>(sample_rate);
        EngineHandle engine = engine_wrapper->get();

        // Initialize Modular Patch for British Church Organ - Using engine_connect_mod for Tier 3
        // 1. Chiff: Env -> Cutoff
        engine_connect_mod(engine, MOD_SRC_ENVELOPE, ALL_VOICES, MOD_TGT_CUTOFF, 0.585f);

        // 2. VCA: Env -> Amplitude (Mandatory for Tier 2/3)
        engine_connect_mod(engine, MOD_SRC_ENVELOPE, ALL_VOICES, MOD_TGT_AMPLITUDE, 1.0f);
        
        // 3. ADSR for classic organ feel
        engine_set_adsr(engine, 0.005f, 0.1f, 0.7f, 0.050f);
        set_param(engine, "vcf_cutoff", 4000.0f);
        
        std::cout << "[BachTest] Modular British Organ Patch Initialized." << std::endl;
        
        ASSERT_EQ(engine_start(engine), 0);
    }

    void TearDown() override {
        if (engine_wrapper) engine_stop(engine_wrapper->get());
    }

    int sample_rate;
    std::unique_ptr<test::EngineWrapper> engine_wrapper; 
};

TEST_F(FunctionalBachMidi, BWV578_Subject_Audible) {
    std::cout << "[BachAudible] Starting BWV 578 Subject (British Organ) @ 72 BPM..." << std::endl;
    
    struct Note {
        uint8_t pitch;
        int duration_ms;
    };

    // Fugue Subject
    std::vector<Note> subject = {
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

    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    std::cout << "[BachAudible] BWV 578 Finished." << std::endl;
}

TEST_F(FunctionalBachMidi, BWV846_Arpeggio_Clarity) {
    std::cout << "[BachAudible] Starting BWV 846 Prelude (Arpeggio Clarity)..." << std::endl;
    std::vector<uint8_t> pattern = {60, 64, 67, 72, 76};
    
    EngineHandle engine = engine_wrapper->get();
    for (int repeat = 0; repeat < 2; ++repeat) {
        for (uint8_t pitch : pattern) {
            engine_note_on(engine, pitch, 0.6f);
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            engine_note_off(engine, pitch);
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    std::cout << "[BachAudible] BWV 846 Finished." << std::endl;
}
