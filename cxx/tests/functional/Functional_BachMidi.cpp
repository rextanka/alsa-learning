#include <gtest/gtest.h>
#include "../../src/core/MidiParser.hpp"
#include "../../src/core/VoiceManager.hpp"
#include "../../src/hal/AudioDriver.hpp"
#ifdef __APPLE__
#include "../../src/hal/coreaudio/CoreAudioDriver.hpp"
#else
#include "../../src/hal/alsa/AlsaDriver.hpp"
#endif
#include "CInterface.h"
#include <vector>
#include <thread>
#include <chrono>

using namespace engine::core;
using namespace audio;

class FunctionalBachMidi : public ::testing::Test {
protected:
    void SetUp() override {
        sample_rate = 44100;
        voiceManager = std::make_unique<VoiceManager>(sample_rate);
#ifdef __APPLE__
        driver = std::make_unique<hal::CoreAudioDriver>(sample_rate, 512);
#else
        driver = std::make_unique<hal::AlsaDriver>(sample_rate, 512);
#endif
        driver->set_stereo_callback([this](audio::AudioBuffer& buffer) {
            voiceManager->pull(buffer);
        });
    }

    void TearDown() override {
        if (driver) driver->stop();
    }

    int sample_rate;
    MidiParser parser;
    std::unique_ptr<VoiceManager> voiceManager;
    std::unique_ptr<hal::AudioDriver> driver;
};

/**
 * BWV 578 Subject (G Minor Fugue)
 * Audible verification of the British Organ.
 */
TEST_F(FunctionalBachMidi, BWV578_Subject_Audible) {
    std::cout << "[BachAudible] Starting BWV 578 Subject (British Organ)..." << std::endl;
    
    struct MidiMessage {
        std::vector<uint8_t> data;
        int delay_ms;
    };

    // Subject: G4, D5, Bb4, A4, G4, Bb4, A4, G4, F#4, A4, D4
    std::vector<MidiMessage> subject = {
        {{0x90, 67, 100}, 400}, // G4
        {{0x80, 67, 0}, 0},
        {{0x90, 74, 100}, 400}, // D5
        {{0x80, 74, 0}, 0},
        {{0x90, 70, 100}, 400}, // Bb4
        {{0x80, 70, 0}, 0},
        {{0x90, 69, 100}, 200}, // A4
        {{0x80, 69, 0}, 0},
        {{0x90, 67, 100}, 200}, // G4
        {{0x80, 67, 0}, 0},
        {{0x90, 70, 100}, 200}, // Bb4
        {{0x80, 70, 0}, 0},
        {{0x90, 69, 100}, 200}, // A4
        {{0x80, 69, 0}, 0},
        {{0x90, 67, 100}, 200}, // G4
        {{0x80, 67, 0}, 0},
        {{0x90, 66, 100}, 200}, // F#4
        {{0x80, 66, 0}, 0},
        {{0x90, 69, 100}, 200}, // A4
        {{0x80, 69, 0}, 0},
        {{0x90, 62, 100}, 600}, // D4
        {{0x80, 62, 0}, 500}
    };

    ASSERT_TRUE(driver->start());

    for (const auto& msg : subject) {
        parser.parse(msg.data.data(), msg.data.size(), 0, [this](const MidiEvent& e) {
            voiceManager->handleMidiEvent(e);
        });
        
        if (msg.delay_ms > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(msg.delay_ms));
        }
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    std::cout << "[BachAudible] BWV 578 Finished." << std::endl;
}

TEST_F(FunctionalBachMidi, RunningStatus_Validation) {
    std::vector<uint8_t> midiData = {
        0x90, 0x43, 0x64, // G4
              0x45, 0x64, // A4 (Running Status)
              0x47, 0x64  // B4 (Running Status)
    };

    std::vector<MidiEvent> events;
    parser.parse(midiData.data(), midiData.size(), 0, [&](const MidiEvent& e) {
        events.push_back(e);
    });

    ASSERT_EQ(events.size(), 3);
    EXPECT_EQ(events[1].status, 0x90);
    EXPECT_EQ(events[1].data1, 0x45);
}
