#include <gtest/gtest.h>
#include "VoiceManager.hpp"
#include "Logger.hpp"

using namespace audio;

TEST(VoiceStressTest, LruStealing) {
    auto& logger = AudioLogger::instance();
    while (logger.pop_entry()) {}

    VoiceManager manager(44100);
    
    // 1. Fill all 16 voices
    for (int i = 0; i < 16; ++i) {
        manager.note_on(60 + i, 0.5f);
    }

    // 2. Trigger one more note (should steal note 60, the oldest)
    manager.note_on(80, 0.5f);

    // Verify via logger if we added telemetry to VoiceManager (we will in next step)
    // For now, check state
    bool note_60_found = false;
    bool note_80_found = false;
    for (const auto& slot : manager.get_voices()) {
        if (slot.active) {
            if (slot.current_note == 60) note_60_found = true;
            if (slot.current_note == 80) note_80_found = true;
        }
    }
    EXPECT_FALSE(note_60_found);
    EXPECT_TRUE(note_80_found);
}

TEST(VoiceStressTest, ReleasePriorityStealing) {
    VoiceManager manager(44100);
    
    // Fill voices
    for (int i = 0; i < 16; ++i) {
        manager.note_on(60 + i, 0.5f);
    }

    // Put note 65 into release
    manager.note_off(65);
    
    // Even if 60 is oldest, 65 is releasing and should be stolen first
    manager.note_on(90, 0.5f);

    bool note_65_found = false;
    bool note_60_found = false;
    bool note_90_found = false;
    for (const auto& slot : manager.get_voices()) {
        if (slot.active) {
            if (slot.current_note == 65) note_65_found = true;
            if (slot.current_note == 60) note_60_found = true;
            if (slot.current_note == 90) note_90_found = true;
        }
    }
    EXPECT_FALSE(note_65_found);
    EXPECT_TRUE(note_60_found);
    EXPECT_TRUE(note_90_found);
}
