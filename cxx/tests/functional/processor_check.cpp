#include <gtest/gtest.h>
#include "Voice.hpp"
#include "oscillator/SineOscillatorProcessor.hpp"
#include "envelope/AdsrEnvelopeProcessor.hpp"
#include "AudioDriver.hpp"
#include <memory>
#include <vector>

namespace audio {

TEST(ProcessorCheck, VoicePullMechanism) {
    const int sample_rate = 44100;
    
    auto voice = std::make_unique<audio::Voice>(sample_rate);
    voice->add_processor(std::make_unique<audio::SineOscillatorProcessor>(sample_rate), "VCO");
    voice->add_processor(std::make_unique<audio::AdsrEnvelopeProcessor>(sample_rate), "VCA");

    voice->note_on(440.0);
    
    std::vector<float> output(512);
    voice->pull(output);
    
    float max_val = 0.0f;
    for (float s : output) if (std::abs(s) > max_val) max_val = std::abs(s);
    EXPECT_GT(max_val, 0.0f);
}

} // namespace audio
