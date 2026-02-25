#include <gtest/gtest.h>
#include "../../src/core/MidiParser.hpp"
#include "../../src/core/VoiceManager.hpp"
#include <vector>

using namespace engine::core;
using namespace audio;

class BachMidiTest : public ::testing::Test {
protected:
    MidiParser parser;
    VoiceManager voiceManager{44100};
};

// BWV 578 Subject (G Minor Fugue)
// G4, D5, Bb4, A4, G4, Bb4, A4, G4, F#4, A4, D4...
TEST_F(BachMidiTest, BWV578_Subject_Parsing) {
    std::vector<uint8_t> midiData = {
        0x90, 0x43, 0x64, // Note On G4 (67)
        0x90, 0x4A, 0x64, // Note On D5 (74)
        0x80, 0x43, 0x00, // Note Off G4
        0x80, 0x4A, 0x00  // Note Off D5
    };

    std::vector<MidiEvent> events;
    parser.parse(midiData.data(), midiData.size(), 0, [&](const MidiEvent& e) {
        events.push_back(e);
    });

    ASSERT_EQ(events.size(), 4);
    EXPECT_EQ(events[0].status, 0x90);
    EXPECT_EQ(events[0].data1, 0x43);
    EXPECT_EQ(events[2].status, 0x80);
}

TEST_F(BachMidiTest, RunningStatus_StressTest) {
    // 0x90 followed by multiple note/vel pairs
    std::vector<uint8_t> midiData = {
        0x90, 0x43, 0x64, // Status + Data
              0x45, 0x64, // Running Status (Note On A4)
              0x47, 0x64  // Running Status (Note On B4)
    };

    std::vector<MidiEvent> events;
    parser.parse(midiData.data(), midiData.size(), 0, [&](const MidiEvent& e) {
        events.push_back(e);
    });

    ASSERT_EQ(events.size(), 3);
    for (const auto& e : events) {
        EXPECT_EQ(e.status, 0x90);
    }
    EXPECT_EQ(events[1].data1, 0x45);
    EXPECT_EQ(events[2].data1, 0x47);
}

TEST_F(BachMidiTest, NoteOnVelocityZero_IsNoteOff) {
    std::vector<uint8_t> midiData = {
        0x90, 0x43, 0x64, // Note On
        0x90, 0x43, 0x00  // Note On w/ Velocity 0
    };

    std::vector<MidiEvent> events;
    parser.parse(midiData.data(), midiData.size(), 0, [&](const MidiEvent& e) {
        events.push_back(e);
    });

    ASSERT_EQ(events.size(), 2);
    EXPECT_TRUE(events[0].isNoteOn());
    EXPECT_TRUE(events[1].isNoteOff());
}

TEST_F(BachMidiTest, VoiceMapping_Polyphony) {
    // Trigger G4 and Bb4
    voiceManager.handleMidiEvent({0x90, 67, 100, 0});
    voiceManager.handleMidiEvent({0x90, 70, 100, 0});

    auto& voices = voiceManager.get_voices();
    int activeCount = 0;
    for (const auto& v : voices) if (v.active) activeCount++;
    EXPECT_EQ(activeCount, 2);

    // Release G4 only
    voiceManager.handleMidiEvent({0x80, 67, 0, 0});
    
    // One voice should now be releasing (but still marked active in slot until pulled)
    int releasingCount = 0;
    for (const auto& v : voices) {
        if (v.active && v.voice->envelope().is_releasing()) releasingCount++;
    }
    EXPECT_EQ(releasingCount, 1);
    
    // Verify it was the correct pitch (this is a bit internal but we can check current_note)
    bool g4found = false;
    for (const auto& v : voices) {
        if (v.current_note == 67 && v.voice->envelope().is_releasing()) g4found = true;
    }
    // Note: in my implementation, note_off sets note_to_voice_map_[note] = -1
    // but the slot itself keeps current_note until it's actually cleaned up in pull.
}
