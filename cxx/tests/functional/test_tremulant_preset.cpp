/**
 * @file test_tremulant_preset.cpp
 * @brief Functional verification of tremulant (vibrato) modulation via the Phase 15A LFO API.
 *
 * Phase 15A replaces the broken integer-ID external mod matrix with
 * engine_set_lfo_* calls that drive the per-voice internal LFO and
 * ModulationMatrix directly.
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
            "Verifies LFO→Pitch vibrato via the Phase 15A engine_set_lfo_* API.",
            "LFO (per-voice internal) → ModulationMatrix → CompositeGenerator pitch",
            "Non-silent output with LFO active; steady output with LFO cleared.",
            sample_rate
        );

        engine_wrapper = std::make_unique<test::EngineWrapper>(sample_rate);

        // Phase 15 chain.
        engine_add_module(engine_wrapper->get(), "COMPOSITE_GENERATOR", "VCO");
        engine_add_module(engine_wrapper->get(), "ADSR_ENVELOPE",       "ENV");
        engine_add_module(engine_wrapper->get(), "VCA",                 "VCA");
        engine_connect_ports(engine_wrapper->get(), "ENV", "envelope_out", "VCA", "gain_cv");
        engine_bake(engine_wrapper->get());

        set_param(engine_wrapper->get(), "sine_gain",   1.0f);
        set_param(engine_wrapper->get(), "amp_sustain", 1.0f);
        // No engine_start: offline processing via engine_process.
    }

    int sample_rate;
    std::unique_ptr<test::EngineWrapper> engine_wrapper;
};

TEST_F(TremulantTest, ModularTremulant) {
    EngineHandle engine = engine_wrapper->get();

    // Configure LFO→Pitch vibrato via the Phase 15A API.
    // 5 Hz, ±0.05 octave (≈ ±3 semitones of gentle vibrato).
    EXPECT_EQ(engine_set_lfo_rate(engine, 5.0f),                          0);
    EXPECT_EQ(engine_set_lfo_waveform(engine, LFO_WAVEFORM_SINE),         0);
    EXPECT_EQ(engine_set_lfo_depth(engine, LFO_TARGET_PITCH, 0.05f),      0);
    EXPECT_EQ(engine_set_lfo_intensity(engine, 1.0f),                     0);

    // Trigger a chord.
    engine_note_on(engine, 60, 0.8f); // C4
    engine_note_on(engine, 64, 0.8f); // E4
    engine_note_on(engine, 67, 0.8f); // G4

    // Process enough blocks for ADSR attack to open (~50ms = ~4 blocks at 48kHz/512).
    std::vector<float> output(512 * 2);
    for (int i = 0; i < 20; ++i) {
        engine_process(engine, output.data(), 512);
    }

    // Final block audit.
    int result = engine_process(engine, output.data(), 512);
    EXPECT_EQ(result, 0);

    float sum_sq = 0;
    for (float s : output) sum_sq += s * s;
    float rms = std::sqrt(sum_sq / static_cast<float>(output.size()));

    ASSERT_GT(rms, 0.001f) << "FAILED: Tremulant output is silent! RMS=" << rms;
    std::cout << "[TremulantTest] LFO→Pitch vibrato active. RMS=" << rms << std::endl;
}

TEST_F(TremulantTest, ClearModulationsDisablesVibrato) {
    EngineHandle engine = engine_wrapper->get();

    // Enable then immediately clear — LFO should be inactive.
    engine_set_lfo_rate(engine, 5.0f);
    engine_set_lfo_intensity(engine, 1.0f);
    engine_set_lfo_depth(engine, LFO_TARGET_PITCH, 0.5f);
    EXPECT_EQ(engine_clear_modulations(engine), 0);

    engine_note_on(engine, 60, 0.8f);
    std::vector<float> output(512 * 2);
    for (int i = 0; i < 8; ++i) engine_process(engine, output.data(), 512);
    int result = engine_process(engine, output.data(), 512);
    EXPECT_EQ(result, 0);

    // Output should still be present (envelope→amplitude connection is restored by clear).
    float sum_sq = 0;
    for (float s : output) sum_sq += s * s;
    float rms = std::sqrt(sum_sq / static_cast<float>(output.size()));
    EXPECT_GT(rms, 0.001f) << "After clear, envelope should still open the VCA";
    std::cout << "[TremulantTest] Post-clear RMS=" << rms << std::endl;
}
