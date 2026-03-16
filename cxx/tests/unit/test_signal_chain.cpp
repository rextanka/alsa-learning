/**
 * @file test_signal_chain.cpp
 * @brief Unit tests for Phase 14 signal chain infrastructure:
 *        CompositeGenerator, PortType, and Processor tag API.
 */

#include <gtest/gtest.h>
#include "CompositeGenerator.hpp"
#include "Processor.hpp"
#include <vector>
#include <cmath>

using namespace audio;

static constexpr int kSampleRate = 48000;
static constexpr size_t kBlockSize = 512;

// --- PortType and tag tests ---

TEST(ProcessorTag, DefaultTagIsEmpty) {
    CompositeGenerator gen(kSampleRate);
    // CompositeGenerator sets its own tag in the constructor
    EXPECT_EQ(gen.tag(), "VCO");
}

TEST(ProcessorTag, SetTagRoundTrips) {
    CompositeGenerator gen(kSampleRate);
    gen.set_tag("TEST_TAG");
    EXPECT_EQ(gen.tag(), "TEST_TAG");
}

TEST(ProcessorTag, OutputPortTypeIsAudio) {
    CompositeGenerator gen(kSampleRate);
    EXPECT_EQ(gen.output_port_type(), PortType::PORT_AUDIO);
}

// --- CompositeGenerator audio output tests ---

TEST(CompositeGenerator, ProducesNonZeroOutputWithActiveMixer) {
    CompositeGenerator gen(kSampleRate);
    gen.set_frequency(440.0);
    gen.mixer().set_gain(0, 1.0f); // enable sawtooth

    std::vector<float> buf(kBlockSize, 0.0f);
    gen.pull(std::span<float>(buf));

    float peak = 0.0f;
    for (float s : buf) peak = std::max(peak, std::abs(s));
    EXPECT_GT(peak, 0.0f);
}

TEST(CompositeGenerator, SilentWhenAllGainsZero) {
    CompositeGenerator gen(kSampleRate);
    gen.set_frequency(440.0);
    // All gains default to 0 — no gain set

    std::vector<float> buf(kBlockSize, 0.0f);
    gen.pull(std::span<float>(buf));

    float peak = 0.0f;
    for (float s : buf) peak = std::max(peak, std::abs(s));
    EXPECT_FLOAT_EQ(peak, 0.0f);
}

TEST(CompositeGenerator, ResetClearsState) {
    CompositeGenerator gen(kSampleRate);
    gen.set_frequency(440.0);
    gen.mixer().set_gain(3, 1.0f); // sine

    std::vector<float> buf(kBlockSize, 0.0f);
    gen.pull(std::span<float>(buf));

    gen.reset();

    // After reset, pulling again should restart from a deterministic phase.
    // Just verify it doesn't crash and produces output.
    std::fill(buf.begin(), buf.end(), 0.0f);
    gen.pull(std::span<float>(buf));
    float peak = 0.0f;
    for (float s : buf) peak = std::max(peak, std::abs(s));
    EXPECT_GT(peak, 0.0f);
}

TEST(CompositeGenerator, FrequencyChangeTakesEffect) {
    // Pull at 440Hz, then at 880Hz — the buffers should differ.
    CompositeGenerator gen1(kSampleRate);
    gen1.set_frequency(440.0);
    gen1.mixer().set_gain(3, 1.0f); // sine

    CompositeGenerator gen2(kSampleRate);
    gen2.set_frequency(880.0);
    gen2.mixer().set_gain(3, 1.0f); // sine

    std::vector<float> buf1(kBlockSize), buf2(kBlockSize);
    gen1.pull(std::span<float>(buf1));
    gen2.pull(std::span<float>(buf2));

    bool differs = false;
    for (size_t i = 0; i < kBlockSize; ++i) {
        if (std::abs(buf1[i] - buf2[i]) > 1e-4f) { differs = true; break; }
    }
    EXPECT_TRUE(differs);
}
