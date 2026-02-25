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
    std::vector<Note> subject = {
        {67, 416}, // G4
        {74, 416}, // D5
        {70, 416}, // Bb4
        {69, 208}, // A4
        {67, 208}, // G4
        {70, 208}, // Bb4
        {69, 208}, // A4
        {67, 208}, // G4
        {66, 208}, // F#4
        {69, 208}, // A4
        {62, 833}  // D4
    };

    ASSERT_TRUE(driver->start());

    // Play loop in a separate thread to avoid blocking GTest/Audio
    std::thread playback([this, subject]() {
        for (const auto& n : subject) {
            voiceManager->handleMidiEvent({0x90, n.pitch, 100, 0});
            std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<int>(n.duration_ms * 0.9)));
            voiceManager->handleMidiEvent({0x80, n.pitch, 0, 0});
            std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<int>(n.duration_ms * 0.1)));
        }
    });

    playback.join();
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    std::cout << "[BachAudible] BWV 578 Finished." << std::endl;
}

/**
 * BWV 846 (Prelude in C) - Opening Arpeggios
 * Stress-tests polyphonic release clarity.
 */
TEST_F(FunctionalBachMidi, BWV846_Arpeggio_Clarity) {
    std::cout << "[BachAudible] Starting BWV 846 Prelude (Arpeggio Clarity)..." << std::endl;

    std::vector<uint8_t> pattern = {60, 64, 67, 72, 76};
    
    ASSERT_TRUE(driver->start());

    std::thread playback([this, pattern]() {
        for (int repeat = 0; repeat < 2; ++repeat) {
            for (uint8_t pitch : pattern) {
                voiceManager->handleMidiEvent({0x90, pitch, 80, 0});
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
                voiceManager->handleMidiEvent({0x80, pitch, 0, 0});
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
            }
        }
    });

    playback.join();
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    std::cout << "[BachAudible] BWV 846 Finished." << std::endl;
}

/**
 * BWV 565 (Toccata Intro)
 * Stress-tests polyphonic impact and voice stealing.
 */
TEST_F(FunctionalBachMidi, BWV565_Toccata_Impact) {
    std::cout << "[BachAudible] Starting BWV 565 Toccata (Polyphonic Impact)..." << std::endl;

    ASSERT_TRUE(driver->start());

    std::thread playback([this]() {
        // Mordent: A4 G4 A4
        uint8_t mordent[] = {69, 67, 69};
        for (uint8_t p : mordent) {
            voiceManager->handleMidiEvent({0x90, p, 110, 0});
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            voiceManager->handleMidiEvent({0x80, p, 0, 0});
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(300));

        // Massive Chord: D2 D3 F3 A3 D4
        uint8_t chord[] = {38, 50, 53, 57, 62};
        for (uint8_t p : chord) {
            voiceManager->handleMidiEvent({0x90, p, 127, 0});
        }
        
        std::this_thread::sleep_for(std::chrono::seconds(2));
        
        for (uint8_t p : chord) {
            voiceManager->handleMidiEvent({0x80, p, 0, 0});
        }
    });

    playback.join();
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    std::cout << "[BachAudible] BWV 565 Finished." << std::endl;
}

TEST_F(FunctionalBachMidi, RunningStatus_Validation) {
    std::vector<uint8_t> midiData = {
        0x90, 0x43, 0x64, 
              0x45, 0x64, 
              0x47, 0x64  
    };

    std::vector<MidiEvent> events;
    parser.parse(midiData.data(), midiData.size(), 0, [&](const MidiEvent& e) {
        events.push_back(e);
    });

    ASSERT_EQ(events.size(), 3);
    EXPECT_EQ(events[1].status, 0x90);
    EXPECT_EQ(events[1].data1, 0x45);
}
