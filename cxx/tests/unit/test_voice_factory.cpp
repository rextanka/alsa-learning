/**
 * @file test_voice_factory.cpp
 * @brief Unit tests for Voice chain construction and bake() validation.
 *
 * Originally tested VoiceFactory (Phase 14). VoiceFactory was retired in
 * Phase 15 — chains are now built via add_processor / connect / bake.
 * The fixture SetUp was updated to build the chain directly; all test logic
 * is unchanged.
 */

#include <gtest/gtest.h>
#include "../../src/core/Voice.hpp"
#include "../../src/dsp/routing/CompositeGenerator.hpp"
#include "../../src/dsp/envelope/AdsrEnvelopeProcessor.hpp"
#include "../../src/dsp/VcaProcessor.hpp"
#include <vector>
#include <cmath>

using namespace audio;

static constexpr int kSR = 48000;
static constexpr size_t kBlock = 512;

class VoiceFactoryTest : public ::testing::Test {
protected:
    std::unique_ptr<Voice> voice;

    void SetUp() override {
        voice = std::make_unique<Voice>(kSR);
        auto gen = std::make_unique<CompositeGenerator>(kSR);
        gen->mixer().set_gain(1, 1.0f); // pulse
        gen->mixer().set_gain(2, 0.5f); // sub
        voice->add_processor(std::move(gen), "VCO");
        auto env = std::make_unique<AdsrEnvelopeProcessor>(kSR);
        env->set_attack_time(0.05f);
        env->set_decay_time(0.10f);
        env->set_sustain_level(0.7f);
        env->set_release_time(0.10f);
        voice->add_processor(std::move(env), "ENV");
        voice->add_processor(std::make_unique<VcaProcessor>(), "VCA");
        voice->connect("ENV", "envelope_out", "VCA", "gain_cv");
        voice->bake();
    }
};

TEST_F(VoiceFactoryTest, VoiceIsBakedAfterCreation) {
    EXPECT_NE(voice->find_by_tag("VCO"), nullptr);
    EXPECT_NE(voice->find_by_tag("ENV"), nullptr);
    EXPECT_NE(voice->find_by_tag("VCA"), nullptr);
}

TEST_F(VoiceFactoryTest, ChainNodesHaveCorrectTags) {
    auto* vco = voice->find_by_tag("VCO");
    ASSERT_NE(vco, nullptr);
    EXPECT_EQ(vco->tag(), "VCO");
    EXPECT_EQ(vco->output_port_type(), PortType::PORT_AUDIO);

    auto* env = voice->find_by_tag("ENV");
    ASSERT_NE(env, nullptr);
    EXPECT_EQ(env->output_port_type(), PortType::PORT_CONTROL);
}

TEST_F(VoiceFactoryTest, VoiceInactiveBeforeNoteOn) {
    EXPECT_FALSE(voice->is_active());
}

TEST_F(VoiceFactoryTest, VoiceActiveAfterNoteOn) {
    voice->note_on(440.0);
    EXPECT_TRUE(voice->is_active());
}

TEST_F(VoiceFactoryTest, ProducesNonZeroAudioAfterNoteOn) {
    voice->note_on(440.0);

    std::vector<float> buf(kBlock, 0.0f);
    voice->pull_mono(std::span<float>(buf));

    float peak = 0.0f;
    for (float s : buf) peak = std::max(peak, std::abs(s));
    EXPECT_GT(peak, 0.0f);
}

TEST_F(VoiceFactoryTest, ReleasingAfterNoteOff) {
    voice->note_on(440.0);
    std::vector<float> buf(kBlock, 0.0f);
    voice->pull_mono(std::span<float>(buf));

    voice->note_off();
    EXPECT_TRUE(voice->is_releasing());
}

TEST_F(VoiceFactoryTest, BakeFailsOnEmptyChain) {
    Voice bare(kSR);
    EXPECT_THROW(bare.bake(), std::logic_error);
}

// PORT_CONTROL nodes (ENV, LFO, …) are auto-routed to mod_sources_ by add_processor.
// A voice with VCO + ENV (no VCA) has signal_chain_=[VCO], mod_sources_=[ENV] — bake() succeeds.
TEST_F(VoiceFactoryTest, PortControlNodesRoutedToModSources) {
    Voice v(kSR);
    v.add_processor(std::make_unique<CompositeGenerator>(kSR), "VCO");
    v.add_processor(std::make_unique<AdsrEnvelopeProcessor>(kSR), "ENV");
    EXPECT_NO_THROW(v.bake());
    // ENV is reachable via find_by_tag (searches both signal_chain_ and mod_sources_).
    auto* env = v.find_by_tag("ENV");
    ASSERT_NE(env, nullptr);
    EXPECT_EQ(env->output_port_type(), PortType::PORT_CONTROL);
}

// --- Phase 15: PortConnection / bake() validation ---

TEST_F(VoiceFactoryTest, ConnectAndBakeSucceedsWithValidPorts) {
    Voice v(kSR);
    v.add_processor(std::make_unique<CompositeGenerator>(kSR), "VCO");
    v.add_processor(std::make_unique<AdsrEnvelopeProcessor>(kSR), "ENV");
    v.add_processor(std::make_unique<VcaProcessor>(), "VCA");
    v.connect("ENV", "envelope_out", "VCA", "gain_cv");
    EXPECT_NO_THROW(v.bake());
    ASSERT_EQ(v.connections().size(), 1u);
    EXPECT_EQ(v.connections()[0].from_tag, "ENV");
    EXPECT_EQ(v.connections()[0].to_tag,   "VCA");
}

TEST_F(VoiceFactoryTest, BakeRejectsLifecyclePortInConnection) {
    Voice v(kSR);
    v.add_processor(std::make_unique<CompositeGenerator>(kSR), "VCO");
    v.add_processor(std::make_unique<AdsrEnvelopeProcessor>(kSR), "ENV");
    v.add_processor(std::make_unique<VcaProcessor>(), "VCA");
    v.connect("VCO", "audio_out", "ENV", "gate_in"); // gate_in is lifecycle — forbidden
    EXPECT_THROW(v.bake(), std::logic_error);
}

TEST_F(VoiceFactoryTest, BakeRejectsUnknownTag) {
    Voice v(kSR);
    v.add_processor(std::make_unique<CompositeGenerator>(kSR), "VCO");
    v.add_processor(std::make_unique<VcaProcessor>(), "VCA");
    v.connect("MISSING", "audio_out", "VCA", "audio_in");
    EXPECT_THROW(v.bake(), std::logic_error);
}

TEST_F(VoiceFactoryTest, BakeRejectsUnknownPortName) {
    Voice v(kSR);
    v.add_processor(std::make_unique<CompositeGenerator>(kSR), "VCO");
    v.add_processor(std::make_unique<VcaProcessor>(), "VCA");
    v.connect("VCO", "nonexistent_port", "VCA", "audio_in");
    EXPECT_THROW(v.bake(), std::logic_error);
}

TEST_F(VoiceFactoryTest, BakeRejectsTypeMismatch) {
    // Connecting a PORT_AUDIO output to a PORT_CONTROL input is invalid.
    Voice v(kSR);
    v.add_processor(std::make_unique<CompositeGenerator>(kSR), "VCO");
    v.add_processor(std::make_unique<AdsrEnvelopeProcessor>(kSR), "ENV");
    v.add_processor(std::make_unique<VcaProcessor>(), "VCA");
    v.connect("VCO", "audio_out", "VCA", "gain_cv"); // PORT_AUDIO → PORT_CONTROL: invalid
    EXPECT_THROW(v.bake(), std::logic_error);
}
