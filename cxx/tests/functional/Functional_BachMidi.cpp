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
 * Audible verification with proper Note Offs and 72 BPM tempo.
 */
TEST_F(FunctionalBachMidi, BWV578_Subject_Audible) {
    std::cout << "[BachAudible] Starting BWV 578 Subject (British Organ) @ 72 BPM..." << std::endl;
    
    struct Note {
        uint8_t pitch;
        int duration_ms;
    };

    // Fugue Subject: G4, D5, Bb4, A4, G4, Bb4, A4, G4, F#4, A4, D4
    // Rhythmic values (72 BPM): 
    // Quarter = 833ms, Eighth = 416ms, Sixteenth = 208ms
    std::vector<Note> subject = {
        {67, 416}, // G4 (8th)
        {74, 416}, // D5 (8th)
        {70, 416}, // Bb4 (8th)
        {69, 208}, // A4 (16th)
        {67, 208}, // G4 (16th)
        {70, 208}, // Bb4 (16th)
        {69, 208}, // A4 (16th)
        {67, 208}, // G4 (16th)
        {66, 208}, // F#4 (16th)
        {69, 208}, // A4 (16th)
        {62, 833}  // D4 (Quarter)
    };

    ASSERT_TRUE(driver->start());

    for (const auto& n : subject) {
        // Note On
        voiceManager->handleMidiEvent({0x90, n.pitch, 100, 0});
        
        // Hold for duration (minus a tiny gap for articulation)
        std::this_thread::sleep_for(std::chrono::milliseconds(n.duration_ms - 20));
        
        // Note Off
        voiceManager->handleMidiEvent({0x80, n.pitch, 0, 0});
        
        // Articulation gap
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
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
