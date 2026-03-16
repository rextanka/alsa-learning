/**
 * @file test_voice_factory.cpp
 * @brief Unit tests for VoiceFactory (Phase 14 Checkpoint E).
 *
 * Verifies that factory-built voices are baked, produce audio, and honour
 * the basic note lifecycle without touching the audio hardware.
 */

#include <gtest/gtest.h>
#include "VoiceFactory.hpp"
#include "routing/CompositeGenerator.hpp"
#include <vector>
#include <cmath>

using namespace audio;

static constexpr int kSR = 48000;
static constexpr size_t kBlock = 512;

class VoiceFactoryTest : public ::testing::Test {
protected:
    std::unique_ptr<Voice> voice;

    void SetUp() override {
        voice = VoiceFactory::createSH101(kSR);
    }
};

TEST_F(VoiceFactoryTest, VoiceIsBakedAfterCreation) {
    // find_by_tag only works on baked voices; if bake() failed it would throw.
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
    // Pull one block to advance the envelope out of Attack
    std::vector<float> buf(kBlock, 0.0f);
    voice->pull_mono(std::span<float>(buf));

    voice->note_off();
    EXPECT_TRUE(voice->is_releasing());
}

TEST_F(VoiceFactoryTest, BakeFailsOnEmptyChain) {
    Voice bare(kSR);
    EXPECT_THROW(bare.bake(), std::logic_error);
}
