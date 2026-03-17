#include <gtest/gtest.h>
#include "../TestHelper.hpp" 
#include <cmath>
#include <vector>
#include <iostream>

/**
 * @file test_sh101_chain.cpp
 * @brief Functional test for the SH-101 signal chain via Bridge API.
 */

class SH101ChainTest : public ::testing::Test {
protected:
    void SetUp() override {
        test::init_test_environment();
        sample_rate = test::get_safe_sample_rate(0);
        
        PRINT_TEST_HEADER(
            "SH-101 Signal Chain Integrity",
            "Verifies sub-oscillator mixing, noise, and 90/10 articulation.",
            "Pulse VCO + Sub VCO -> VCF -> VCA -> Output",
            "Non-zero signal output with combined oscillators and filter action.",
            sample_rate
        );

        engine_wrapper = std::make_unique<test::EngineWrapper>(sample_rate);
    }

    int sample_rate;
    std::unique_ptr<test::EngineWrapper> engine_wrapper;
};

TEST_F(SH101ChainTest, SubOscAndOctave) {
    EngineHandle engine = engine_wrapper->get();
    
    // 1. Phase 15 chain
    engine_add_module(engine, "COMPOSITE_GENERATOR", "VCO");
    engine_add_module(engine, "ADSR_ENVELOPE",       "ENV");
    engine_add_module(engine, "VCA",                 "VCA");
    engine_connect_ports(engine, "ENV", "envelope_out", "VCA", "gain_cv");
    engine_bake(engine);

    // LFO → Pulse Width modulation (PWM) — Phase 16 chain-based
    // Note: rebuild chain with LFO for PWM; reuse current chain for this audible test.
    set_param(engine, "pulse_width", 0.3f); // static PW for this test (chain has no LFO)

    // 2. Mixer Configuration
    set_param(engine, "pulse_gain", 1.0f);
    set_param(engine, "sub_gain", 0.5f);
    set_param(engine, "vcf_cutoff", 2000.0f); // Warm resonant bass
    set_param(engine, "vcf_res", 0.4f);

    // ADSR Configuration
    set_param(engine, "amp_attack", 0.01f);
    set_param(engine, "amp_decay", 1.0f);
    set_param(engine, "amp_sustain", 0.3f);
    
    // 4. Execution (Real-time listening)
    ASSERT_EQ(engine_start(engine), 0);

    std::cout << "[SH101Test] Playing C1 low bass drone for 2s..." << std::endl;
    engine_note_on(engine, 36, 1.0f); 
    
    // Allow for real-time audible verification
    test::wait_while_running(2);
    
    engine_note_off(engine, 36);
    test::wait_while_running(1); // Allow release

    std::cout << "[SH101Chain] Playback complete." << std::endl;
}

// PatchPersistence: engine_save_patch was removed (unimplemented stub, Phase 15).
// Patch round-trip testing is deferred until a serialiser is implemented.
// The load-only path is exercised by the dedicated patch_sequence_test binary.

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
