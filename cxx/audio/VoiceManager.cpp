/**
 * @file VoiceManager.cpp
 * @brief Implementation of the VoiceManager class.
 */

#include "VoiceManager.hpp"
#include <cmath>
#include <algorithm>

namespace audio {

VoiceManager::VoiceManager(int sample_rate)
    : sample_rate_(sample_rate)
{
    for (auto& slot : voices_) {
        slot.voice = std::make_unique<Voice>(sample_rate);
        slot.current_note = -1;
        slot.active = false;
    }
}

void VoiceManager::note_on(int note, float /* velocity */) {
    // 1. Check if the note is already playing (re-trigger)
    for (auto& slot : voices_) {
        if (slot.active && slot.current_note == note) {
            slot.voice->note_on(note_to_freq(note));
            return;
        }
    }

    // 2. Find an idle voice
    for (auto& slot : voices_) {
        if (!slot.voice->is_active()) {
            slot.current_note = note;
            slot.active = true;
            slot.voice->note_on(note_to_freq(note));
            return;
        }
    }

    // 3. Voice Stealing: If all voices are busy, steal a voice.
    // Strategy: "Quiet Phase" (steal a voice that is already in the Release stage).
    // If no voice is in release, we'll just use the first voice in the pool (simple stealing).
    VoiceSlot* steal_candidate = nullptr;
    for (auto& slot : voices_) {
        // We know they are all active because we didn't return in step 2.
        // If we find one that is active but logically in note_off state, it's a prime candidate.
        // We can check if the ADSR is in release by adding a method or assuming the voice
        // state. For now, since we haven't implemented explicit stage query, 
        // we'll just steal the first slot as a fallback.
        steal_candidate = &slot;
        break; 
    }

    if (steal_candidate) {
        steal_candidate->current_note = note;
        steal_candidate->active = true;
        steal_candidate->voice->reset(); // Hard reset to avoid artifacts if stealing
        steal_candidate->voice->note_on(note_to_freq(note));
    }
}

void VoiceManager::note_off(int note) {
    for (auto& slot : voices_) {
        if (slot.active && slot.current_note == note) {
            slot.voice->note_off();
            // Note: we don't set active = false here because the release phase
            // is still playing. is_active() will handle that in do_pull.
        }
    }
}

void VoiceManager::reset() {
    for (auto& slot : voices_) {
        slot.voice->reset();
        slot.current_note = -1;
        slot.active = false;
    }
}

void VoiceManager::do_pull(std::span<float> output, const VoiceContext* context) {
    // Zero out the buffer before summing
    std::fill(output.begin(), output.end(), 0.0f);

    static thread_local std::vector<float> voice_buffer;
    if (voice_buffer.size() < output.size()) {
        voice_buffer.resize(output.size());
    }
    std::span<float> voice_span(voice_buffer.data(), output.size());

    for (auto& slot : voices_) {
        if (slot.active) {
            if (slot.voice->is_active()) {
                slot.voice->pull(voice_span, context);
                for (size_t i = 0; i < output.size(); ++i) {
                    output[i] += voice_span[i];
                }
            } else {
                slot.active = false;
                slot.current_note = -1;
            }
        }
    }
    
    // Simple master gain to prevent clipping if many voices are playing
    // For 16 voices, 0.25 is a safe-ish bet for non-square waves.
    for (auto& sample : output) {
        sample *= 0.25f;
    }
}

double VoiceManager::note_to_freq(int note) const {
    return 440.0 * std::pow(2.0, (note - 69) / 12.0);
}

} // namespace audio
