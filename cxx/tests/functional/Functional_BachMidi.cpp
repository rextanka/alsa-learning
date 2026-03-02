#include <gtest/gtest.h>
#include "VoiceManager.hpp"
#include "AudioDriver.hpp"
#include "MidiParser.hpp"
#include "MusicalClock.hpp"
#include "envelope/AdsrEnvelopeProcessor.hpp"
#include <memory>
#include <vector>
#include <iostream>

namespace audio {

class Functional_BachMidi : public ::testing::Test {
protected:
    void SetUp() override {
        sample_rate = 44100;
        voiceManager = std::make_unique<VoiceManager>(sample_rate);
    }

    int sample_rate;
    std::unique_ptr<VoiceManager> voiceManager;
};

TEST_F(Functional_BachMidi, PolyphonicPlayback_Stability) {
    // Basic stability test for polyphonic MIDI playback
    // 1. Play a C major chord
    voiceManager->note_on(60, 0.8f);
    voiceManager->note_on(64, 0.8f);
    voiceManager->note_on(67, 0.8f);
    
    std::vector<float> output(512);
    voiceManager->pull(output);
    
    float peak = 0.0f;
    for (float s : output) if (std::abs(s) > peak) peak = std::abs(s);
    EXPECT_GT(peak, 0.01f);
    
    // 2. Release
    voiceManager->note_off(60);
    voiceManager->note_off(64);
    voiceManager->note_off(67);
    
    voiceManager->pull(output);
    // Should still have some tail
}

} // namespace audio
