#include <gtest/gtest.h>
#include "VoiceManager.hpp"
#include "envelope/AdsrEnvelopeProcessor.hpp"
#include "oscillator/SawtoothOscillatorProcessor.hpp"
#include "filter/MoogLadderProcessor.hpp"
#include <vector>

namespace audio {

TEST(VoiceStressTest, ReleasePriorityStealing) {
    const int sample_rate = 44100;
    VoiceManager manager(sample_rate);
    
    // We rely on the VoiceManager's default VCO+VCA setup which includes an AdsrEnvelopeProcessor
    
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

} // namespace audio
