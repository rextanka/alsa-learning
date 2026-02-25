#include <gtest/gtest.h>
#include "../../src/core/ModulationMatrix.hpp"
#include <iostream>
#include <cmath>

using namespace audio;

class ModulationMatrixTest : public ::testing::Test {
protected:
    void SetUp() override {
        matrix.clear_all();
        source_values.fill(0.0f);
    }

    ModulationMatrix matrix;
    std::array<float, static_cast<size_t>(ModulationSource::Count)> source_values;

    void log_audit(const char* target_name, float base, float sum, float final_val) {
        std::cout << "[AUDIT] Target: " << target_name 
                  << " | Base: " << base 
                  << " | Sum: " << sum 
                  << " | Final: " << final_val << std::endl;
    }
};

TEST_F(ModulationMatrixTest, AccumulationPattern) {
    // Set source values
    source_values[static_cast<size_t>(ModulationSource::Envelope)] = 1.0f;
    source_values[static_cast<size_t>(ModulationSource::LFO)] = 0.5f;

    // Connect both to Cutoff
    matrix.set_connection(ModulationSource::Envelope, ModulationTarget::Cutoff, 0.5f);
    matrix.set_connection(ModulationSource::LFO, ModulationTarget::Cutoff, 0.2f);

    float sum = matrix.sum_for_target(ModulationTarget::Cutoff, source_values);
    float base_cutoff = 1000.0f;
    float final_cutoff = base_cutoff * std::pow(2.0f, sum);

    log_audit("Cutoff Accumulation", base_cutoff, sum, final_cutoff);

    // Expected sum: (1.0 * 0.5) + (0.5 * 0.2) = 0.5 + 0.1 = 0.6
    EXPECT_NEAR(sum, 0.6f, 0.001f);
    EXPECT_NEAR(final_cutoff, base_cutoff * std::pow(2.0f, 0.6f), 0.1f);
}

TEST_F(ModulationMatrixTest, ModulationInversion) {
    source_values[static_cast<size_t>(ModulationSource::Envelope)] = 1.0f;

    // Inverted envelope: -1.0 octaves
    matrix.set_connection(ModulationSource::Envelope, ModulationTarget::Cutoff, -1.0f);

    float sum = matrix.sum_for_target(ModulationTarget::Cutoff, source_values);
    float base_cutoff = 1000.0f;
    float final_cutoff = base_cutoff * std::pow(2.0f, sum);

    log_audit("Cutoff Inversion", base_cutoff, sum, final_cutoff);

    EXPECT_NEAR(sum, -1.0f, 0.001f);
    EXPECT_NEAR(final_cutoff, 500.0f, 0.1f); // One octave down
}

TEST_F(ModulationMatrixTest, PitchDoublingVerification) {
    source_values[static_cast<size_t>(ModulationSource::LFO)] = 1.0f;

    // +1.0 octave modulation
    matrix.set_connection(ModulationSource::LFO, ModulationTarget::Pitch, 1.0f);

    float sum = matrix.sum_for_target(ModulationTarget::Pitch, source_values);
    double base_pitch = 440.0;
    double final_pitch = base_pitch * std::pow(2.0, static_cast<double>(sum));

    log_audit("Pitch Doubling", static_cast<float>(base_pitch), sum, static_cast<float>(final_pitch));

    EXPECT_NEAR(sum, 1.0f, 0.001f);
    EXPECT_DOUBLE_EQ(final_pitch, 880.0);
}

TEST_F(ModulationMatrixTest, ZeroCrossingSafety) {
    source_values[static_cast<size_t>(ModulationSource::Envelope)] = 1.0f;

    // Extreme negative modulation: -10 octaves
    matrix.set_connection(ModulationSource::Envelope, ModulationTarget::Cutoff, -10.0f);

    float sum = matrix.sum_for_target(ModulationTarget::Cutoff, source_values);
    float base_cutoff = 1000.0f;
    float final_cutoff = base_cutoff * std::pow(2.0f, sum);
    
    // Manual clamp simulation as in Voice.cpp
    float clamped_cutoff = std::max(20.0f, final_cutoff);

    log_audit("Zero Crossing (Extreme Neg)", base_cutoff, sum, clamped_cutoff);

    EXPECT_NEAR(sum, -10.0f, 0.001f);
    EXPECT_GE(clamped_cutoff, 20.0f);
}
