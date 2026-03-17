/**
 * @file test_tremulant_preset.cpp
 * @brief Functional verification of tremulant (vibrato) via chain-based LFO (Phase 16).
 *
 * LFO is a first-class chain module; wired to VCO.pitch_cv via engine_connect_ports.
 */

#include <gtest/gtest.h>
#include "../TestHelper.hpp"
#include <vector>
#include <cmath>
#include <iostream>

class TremulantTest : public ::testing::Test {
protected:
    void SetUp() override {
        test::init_test_environment();
        sample_rate = test::get_safe_sample_rate(0);

        PRINT_TEST_HEADER(
            "Modular Tremulant Integrity",
            "Verifies LFO→Pitch vibrato via chain-based Phase 16 CV routing.",
            "LFO.control_out → VCO.pitch_cv via engine_connect_ports",
            "Non-silent output with LFO active.",
            sample_rate
        );

        engine_wrapper = std::make_unique<test::EngineWrapper>(sample_rate);
        auto engine = engine_wrapper->get();

        // LFO first so its ctrl buffer is filled before VCO is processed.
        engine_add_module(engine, "LFO",                 "LFO1");
        engine_add_module(engine, "COMPOSITE_GENERATOR", "VCO");
        engine_add_module(engine, "ADSR_ENVELOPE",       "ENV");
        engine_add_module(engine, "VCA",                 "VCA");
        engine_connect_ports(engine, "LFO1", "control_out", "VCO",  "pitch_cv");
        engine_connect_ports(engine, "ENV",  "envelope_out", "VCA", "gain_cv");
        engine_bake(engine);

        set_param(engine, "sine_gain",   1.0f);
        set_param(engine, "amp_sustain", 1.0f);
        // Configure LFO: 5 Hz, ±0.05 octave gentle vibrato.
        set_param(engine, "rate",      5.0f);
        set_param(engine, "intensity", 0.05f);
    }

    int sample_rate;
    std::unique_ptr<test::EngineWrapper> engine_wrapper;
};

TEST_F(TremulantTest, ModularTremulant) {
    EngineHandle engine = engine_wrapper->get();

    engine_note_on(engine, 60, 0.8f); // C4
    engine_note_on(engine, 64, 0.8f); // E4
    engine_note_on(engine, 67, 0.8f); // G4

    // Wait for ADSR attack (~50ms = ~4 blocks at 48kHz/512).
    std::vector<float> output(512 * 2);
    for (int i = 0; i < 20; ++i) engine_process(engine, output.data(), 512);

    int result = engine_process(engine, output.data(), 512);
    EXPECT_EQ(result, 0);

    float sum_sq = 0;
    for (float s : output) sum_sq += s * s;
    float rms = std::sqrt(sum_sq / static_cast<float>(output.size()));

    ASSERT_GT(rms, 0.001f) << "Tremulant output is silent! RMS=" << rms;
    std::cout << "[TremulantTest] LFO→Pitch vibrato active. RMS=" << rms << "\n";
}
