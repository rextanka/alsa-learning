#include <gtest/gtest.h>
#include "AdsrEnvelopeProcessor.hpp"
#include "VcaProcessor.hpp"
#include <vector>

using namespace audio;

TEST(EnvelopeTest, ZeroCrossing) {
    int sample_rate = 44100;
    AdsrEnvelopeProcessor env(sample_rate);
    
    // Set fast parameters
    env.set_attack_time(0.001f); // 44.1 samples
    env.set_decay_time(0.001f);
    env.set_sustain_level(0.5f);
    env.set_release_time(0.001f);

    env.gate_on();
    
    std::vector<float> buffer(1024);
    std::span<float> span(buffer);
    
    // Buffer contents are irrelevant — envelope fills (not multiplies) the output.
    std::fill(buffer.begin(), buffer.end(), 1.0f);
    
    // Process a bunch of samples
    env.pull(span);
    EXPECT_GT(buffer[0], 0.0f);
    
    env.gate_off();
    
    // Process enough to reach zero
    for(int i=0; i<10; ++i) {
        env.pull(span);
    }
    
    EXPECT_FALSE(env.is_active());
    
    env.pull(span);
    EXPECT_EQ(buffer[0], 0.0f);
}

// Verify the VCA/Envelope separation contract (MODULE_DESC §3):
// envelope->pull() must fill output with level values, not multiply audio.
// VcaProcessor::apply() must then perform the audio multiplication.
TEST(EnvelopeTest, OutputIsLevelNotMultiply) {
    AdsrEnvelopeProcessor env(44100);
    env.set_attack_time(0.1f);
    env.set_sustain_level(0.5f);

    env.gate_on();

    std::vector<float> env_buf(512);
    // Pre-fill with a sentinel value that would expose in-place multiplication
    std::fill(env_buf.begin(), env_buf.end(), 99.0f);
    env.pull(std::span<float>(env_buf));

    // If envelope multiplied in-place: buf[0] = 99.0 * level (>> 1.0)
    // If envelope fills correctly:    buf[0] = level (in [0,1])
    EXPECT_GE(env_buf[0], 0.0f);
    EXPECT_LE(env_buf[0], 1.0f);

    // Verify VCA applies the envelope to audio correctly
    std::vector<float> audio(512, 1.0f);
    std::vector<float> gain(512, 0.5f);
    VcaProcessor::apply(std::span<float>(audio),
                        std::span<const float>(gain), 1.0f);
    EXPECT_FLOAT_EQ(audio[0], 0.5f);
}

TEST(EnvelopeTest, SustainLevelZero) {
    AdsrEnvelopeProcessor env(44100);
    env.set_sustain_level(0.0f);
    env.set_attack_time(0.01f);
    env.set_decay_time(0.01f);
    
    env.gate_on();
    
    std::vector<float> buffer(44100); // 1 second
    env.pull(std::span<float>(buffer));
    
    // Should have reached sustain (0.0) and stayed there
    EXPECT_EQ(buffer.back(), 0.0f);
    // But it's still "active" because gate is on
    EXPECT_TRUE(env.is_active());
}
