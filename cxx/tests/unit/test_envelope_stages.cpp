#include <gtest/gtest.h>
#include "AdsrEnvelopeProcessor.hpp"
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
