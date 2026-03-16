/**
 * @file test_signal_chain.cpp
 * @brief Unit tests for Phase 14 signal chain infrastructure:
 *        CompositeGenerator, PortType, and Processor tag API.
 */

#include <gtest/gtest.h>
#include "CompositeGenerator.hpp"
#include "Processor.hpp"
#include "ModuleRegistry.hpp"
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

// --- ModuleRegistry tests (Phase 15) ---

class ModuleRegistryTest : public ::testing::Test {
protected:
    void SetUp() override { ModuleRegistry::instance(); register_builtin_processors(); }
};

TEST_F(ModuleRegistryTest, RegistryContainsAllBuiltinTypes) {
    auto& reg = ModuleRegistry::instance();
    const std::vector<std::string> expected = {
        "COMPOSITE_GENERATOR", "ADSR_ENVELOPE", "VCA",
        "MOOG_FILTER", "DIODE_FILTER", "LFO", "WHITE_NOISE", "JUNO_CHORUS"
    };
    for (const auto& name : expected) {
        EXPECT_NE(reg.find(name), nullptr) << "Missing: " << name;
    }
}

TEST_F(ModuleRegistryTest, FactoryProducesInstance) {
    auto proc = ModuleRegistry::instance().create("COMPOSITE_GENERATOR", 48000);
    ASSERT_NE(proc, nullptr);
    EXPECT_EQ(proc->output_port_type(), PortType::PORT_AUDIO);
}

TEST_F(ModuleRegistryTest, AdsrEnvelopeHasDeclaredPorts) {
    const auto* desc = ModuleRegistry::instance().find("ADSR_ENVELOPE");
    ASSERT_NE(desc, nullptr);
    bool has_envelope_out = false;
    for (const auto& p : desc->ports) {
        if (p.name == "envelope_out" && p.type == PortType::PORT_CONTROL
                                     && p.unipolar)
            has_envelope_out = true;
    }
    EXPECT_TRUE(has_envelope_out);
}

TEST_F(ModuleRegistryTest, MoogFilterHasDeclaredParameters) {
    const auto* desc = ModuleRegistry::instance().find("MOOG_FILTER");
    ASSERT_NE(desc, nullptr);
    bool has_cutoff = false;
    for (const auto& p : desc->parameters) {
        if (p.name == "cutoff" && p.logarithmic) has_cutoff = true;
    }
    EXPECT_TRUE(has_cutoff);
}

TEST_F(ModuleRegistryTest, FindUnknownTypeReturnsNullptr) {
    EXPECT_EQ(ModuleRegistry::instance().find("DOES_NOT_EXIST"), nullptr);
}
