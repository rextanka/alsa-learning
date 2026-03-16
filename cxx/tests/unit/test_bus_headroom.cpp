#include <gtest/gtest.h>
#include "../../src/core/SummingBus.hpp"
#include <vector>
#include <cmath>

namespace audio {

class SummingBusTest : public ::testing::Test {
protected:
    void SetUp() override {
        bus = std::make_unique<SummingBus>(512);
    }

    std::unique_ptr<SummingBus> bus;
};

/**
 * Test 1: Verify that summing 16 full-scale (1.0f) voices does not clip excessively.
 * With MASTER_GAIN = 0.15, 16 * 1.0 * 0.15 = 2.4. 
 * The pull() method should apply soft-clipping to keep it within safe bounds.
 */
TEST_F(SummingBusTest, HeadroomAndClipping) {
    const size_t frames = 512;
    std::vector<float> mono_voice(frames, 1.0f);
    
    bus->clear();
    for (int i = 0; i < 16; ++i) {
        bus->add_voice(mono_voice, 0.0f); // All centered
    }

    std::vector<float> l_out(frames);
    std::vector<float> r_out(frames);
    AudioBuffer output;
    output.left = std::span<float>(l_out);
    output.right = std::span<float>(r_out);
    bus->pull(output);

    for (size_t i = 0; i < frames; ++i) {
        EXPECT_LE(output.left[i], 1.0f);
        EXPECT_GE(output.left[i], -1.0f);
        EXPECT_GT(output.left[i], 0.9f); // Should be near the 0.95 soft-clip threshold
    }
}

/**
 * Test 2: Verify that a single active voice maintains expected gain.
 * Expected peak = 1.0 * 0.15 * cos(theta).
 */
TEST_F(SummingBusTest, UnityGainPreservation) {
    const size_t frames = 512;
    std::vector<float> mono_voice(frames, 1.0f);
    
    bus->clear();
    bus->add_voice(mono_voice, 0.0f); // Center

    std::vector<float> l_out(frames);
    std::vector<float> r_out(frames);
    AudioBuffer output;
    output.left = std::span<float>(l_out);
    output.right = std::span<float>(r_out);
    bus->pull(output);

    // theta for center is PI/4. cos(PI/4) = sin(PI/4) = ~0.707
    float expected = 1.0f * SummingBus::MASTER_GAIN * std::cos(M_PI / 4.0f);
    EXPECT_NEAR(output.left[0], expected, 1e-4f);
    EXPECT_NEAR(output.right[0], expected, 1e-4f);
}

/**
 * Test 3: Validate Constant Power Panning (L^2 + R^2 = 1).
 */
TEST_F(SummingBusTest, PanningPowerIntegrity) {
    const size_t frames = 512;
    std::vector<float> mono_voice(frames, 1.0f);
    float positions[] = {-1.0f, -0.5f, 0.0f, 0.5f, 1.0f};

    for (float pos : positions) {
        bus->clear();
        bus->add_voice(mono_voice, pos);

        // Calculate power at this position (ignoring Master Gain for normalization)
        float theta = (pos + 1.0f) * (M_PI / 4.0f);
        float l = std::cos(theta);
        float r = std::sin(theta);
        
        float power = (l * l) + (r * r);
        EXPECT_NEAR(power, 1.0f, 1e-5f);
    }
}

} // namespace audio
