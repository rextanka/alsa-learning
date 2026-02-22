#include <gtest/gtest.h>
#include "oscillator/WavetableOscillatorProcessor.hpp"
#include <vector>
#include <algorithm>

using namespace audio;

class OscillatorTest : public ::testing::Test {
protected:
    const double sample_rate = 44100.0;
    const int block_size = 128;
};

TEST_F(OscillatorTest, SignalGeneration) {
    WavetableOscillatorProcessor osc(sample_rate);
    osc.setFrequency(440.0);
    osc.setWaveType(WaveType::Sine);

    std::vector<float> buffer(block_size, 0.0f);
    std::span<float> output(buffer);

    osc.pull(output);

    float max_val = 0.0f;
    for (float sample : buffer) {
        max_val = std::max(max_val, std::abs(sample));
    }

    // Ensure the oscillator is actually producing a signal
    EXPECT_GT(max_val, 0.0f);
    EXPECT_LE(max_val, 1.0f);
}

TEST_F(OscillatorTest, FrequencyChange) {
    WavetableOscillatorProcessor osc(sample_rate);
    osc.setFrequency(10.0); // Very low frequency
    
    std::vector<float> buffer1(block_size, 0.0f);
    osc.pull(std::span<float>(buffer1));
    
    osc.reset();
    osc.setFrequency(1000.0); // Higher frequency
    
    std::vector<float> buffer2(block_size, 0.0f);
    osc.pull(std::span<float>(buffer2));
    
    // The buffers should be significantly different
    float diff = 0.0f;
    for (size_t i = 0; i < block_size; ++i) {
        diff += std::abs(buffer1[i] - buffer2[i]);
    }
    
    EXPECT_GT(diff, 0.1f);
}

TEST_F(OscillatorTest, ResetPhase) {
    WavetableOscillatorProcessor osc(sample_rate);
    osc.setFrequency(440.0);
    
    std::vector<float> buffer1(block_size);
    osc.pull(std::span<float>(buffer1));
    
    osc.reset();
    osc.setFrequency(440.0); // Same frequency
    
    std::vector<float> buffer2(block_size);
    osc.pull(std::span<float>(buffer2));
    
    // After reset, the output should be identical for the same frequency
    for (size_t i = 0; i < block_size; ++i) {
        EXPECT_NEAR(buffer1[i], buffer2[i], 1e-6f);
    }
}
