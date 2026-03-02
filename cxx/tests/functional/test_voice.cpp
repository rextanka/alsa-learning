#include <gtest/gtest.h>
#include "Voice.hpp"
#include "oscillator/SawtoothOscillatorProcessor.hpp"
#include "envelope/AdsrEnvelopeProcessor.hpp"
#include "filter/MoogLadderProcessor.hpp"
#include <vector>

namespace audio {

/**
 * @brief Test the new Flexible Voice Topology.
 */
TEST(VoiceTopologyTest, DynamicChainProcessing) {
    const int sample_rate = 44100;
    auto voice = std::make_unique<audio::Voice>(sample_rate);
    
    // Build a simple chain: Saw -> ADSR
    voice->add_processor(std::make_unique<audio::SawtoothOscillatorProcessor>(sample_rate), "VCO");
    voice->add_processor(std::make_unique<audio::AdsrEnvelopeProcessor>(sample_rate), "VCA");
    
    // Configure ADSR for quick test
    auto* adsr = dynamic_cast<audio::AdsrEnvelopeProcessor*>(voice->get_processor_by_tag("VCA"));
    ASSERT_NE(adsr, nullptr);
    adsr->set_attack_time(0.01f);
    adsr->set_sustain_level(1.0f);
    
    // Map a parameter
    voice->register_parameter(10, "VCO", 0); // Param 10 -> VCO Frequency (internal ID 0)
    
    // Trigger
    voice->note_on(440.0);
    EXPECT_TRUE(voice->is_active());
    
    std::vector<float> output(512);
    voice->pull(output);
    
    float max_val = 0.0f;
    for (float s : output) if (std::abs(s) > max_val) max_val = std::abs(s);
    
    EXPECT_GT(max_val, 0.0f);
    
    // Note off
    voice->note_off();
    // Should still be active during release (default release is non-zero)
    EXPECT_TRUE(voice->is_active());
}

TEST(VoiceTopologyTest, ParameterMapping) {
    const int sample_rate = 44100;
    auto voice = std::make_unique<audio::Voice>(sample_rate);
    
    voice->add_processor(std::make_unique<audio::MoogLadderProcessor>(sample_rate), "VCF");
    voice->register_parameter(1, "VCF", 1); // Param 1 -> Cutoff (internal ID 1)
    
    voice->set_parameter(1, 500.0f);
    // In MoogLadderProcessor, internal ID 1 is cutoff.
    // We'd need to verify it was set, but for now we verify it doesn't crash.
}

} // namespace audio

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
