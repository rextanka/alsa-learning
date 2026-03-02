#include <gtest/gtest.h>
#include "Voice.hpp"
#include "oscillator/WavetableOscillatorProcessor.hpp"
#include "filter/MoogLadderProcessor.hpp"
#include "AudioDriver.hpp"
#include <memory>
#include <vector>

namespace audio {

TEST(FilterTest, BasicFilterSweep) {
    const int sample_rate = 44100;
    
    auto voice = std::make_unique<audio::Voice>(sample_rate);
    
    // Setup Flexible Topology for Test
    voice->add_processor(std::make_unique<audio::WavetableOscillatorProcessor>(
        static_cast<double>(sample_rate), 2048, audio::WaveType::Saw), "VCO");
    voice->add_processor(std::make_unique<audio::MoogLadderProcessor>(sample_rate), "VCF");
    
    // Map parameters
    voice->register_parameter(1, "VCF", 1); // Cutoff
    voice->register_parameter(2, "VCF", 2); // Resonance

    voice->note_on(220.0);
    
    // Sweep cutoff
    for (int i = 0; i < 50; ++i) {
        voice->set_parameter(1, 100.0f + i * 100.0f);
        voice->set_parameter(2, i / 50.0f);
        
        // Simulate a few blocks of processing
        AudioBuffer buffer;
        buffer.resize(512, 1);
        voice->pull(buffer.left);
    }
    
    voice->note_off();
    EXPECT_TRUE(true); // Should not crash
}

} // namespace audio
