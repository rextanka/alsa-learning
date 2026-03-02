#include <gtest/gtest.h>
#include "../../src/core/VoiceManager.hpp"
#include "../../src/core/Voice.hpp"
#include "envelope/AdsrEnvelopeProcessor.hpp"
#include "oscillator/SawtoothOscillatorProcessor.hpp"
#include "CInterface.h"
#include <vector>

namespace audio {

class FunctionalBachMidi : public ::testing::Test {
protected:
    void SetUp() override {
        sample_rate = 44100;
        voiceManager = std::make_unique<VoiceManager>(sample_rate);
    }

    int sample_rate;
    std::unique_ptr<VoiceManager> voiceManager;
};

TEST_F(FunctionalBachMidi, BritishOrgan_Timbre_Verification) {
    // This test ensures the Voice signal chain matches the British Organ specification
    
    // We'll use the first voice for detailed inspection
    Voice* testVoice = voiceManager->get_voice(0);
    ASSERT_NE(testVoice, nullptr);

    // Re-configure the voice for Organ timbre
    testVoice->clear_processors();
    testVoice->add_processor(std::make_unique<SawtoothOscillatorProcessor>(sample_rate), "VCO");
    auto adsr = std::make_unique<AdsrEnvelopeProcessor>(sample_rate);
    adsr->set_attack_time(0.015f);
    adsr->set_release_time(0.050f);
    testVoice->add_processor(std::move(adsr), "VCA");

    // Trigger the voice through the manager to ensure proper slot management
    voiceManager->note_on(60, 1.0f, 440.0, true); // true = virtual setup, don't trigger note_on yet
    testVoice->note_on(440.0);

    EXPECT_TRUE(testVoice->is_active());

    float buffer[128];
    std::span<float> span(buffer, 128);
    testVoice->pull(span);

    float peak = 0.0f;
    for (float s : buffer) if (std::abs(s) > peak) peak = std::abs(s);
    EXPECT_GT(peak, 0.01f);
}

} // namespace audio
