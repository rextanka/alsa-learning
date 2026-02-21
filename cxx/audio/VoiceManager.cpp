/**
 * @file VoiceManager.cpp
 * @brief Implementation of the VoiceManager class with advanced management.
 */

#include "VoiceManager.hpp"
#include <cmath>
#include <algorithm>
#include <limits>

namespace audio {

VoiceManager::VoiceManager(int sample_rate)
    : sample_rate_(sample_rate)
    , timestamp_counter_(0)
{
    for (auto& slot : voices_) {
        slot.voice = std::make_unique<Voice>(sample_rate);
        slot.current_note = -1;
        slot.active = false;
        slot.last_note_on_time = 0;
    }
}

void VoiceManager::note_on(int note, float /* velocity */) {
    // 1. Check if the note is already playing (re-trigger)
    for (auto& slot : voices_) {
        if (slot.active && slot.current_note == note) {
            slot.last_note_on_time = next_timestamp();
            slot.voice->note_on(note_to_freq(note));
            return;
        }
    }

    // 2. Find an idle voice
    for (auto& slot : voices_) {
        if (!slot.voice->is_active()) {
            slot.current_note = note;
            slot.active = true;
            slot.last_note_on_time = next_timestamp();
            slot.voice->note_on(note_to_freq(note));
            return;
        }
    }

    // 3. Voice Stealing: Priority (Idle > Releasing > Oldest Active)
    VoiceSlot* candidate = nullptr;
    
    // Priority 1: Find Releasing Voice
    for (auto& slot : voices_) {
        if (slot.voice->envelope().is_releasing()) {
            candidate = &slot;
            break;
        }
    }

    // Priority 2: Steal Oldest Active Voice
    if (!candidate) {
        uint64_t oldest_time = std::numeric_limits<uint64_t>::max();
        for (auto& slot : voices_) {
            if (slot.last_note_on_time < oldest_time) {
                oldest_time = slot.last_note_on_time;
                candidate = &slot;
            }
        }
    }

    if (candidate) {
        candidate->current_note = note;
        candidate->active = true;
        candidate->last_note_on_time = next_timestamp();
        candidate->voice->reset(); // Avoid artifacts
        candidate->voice->set_pan(0.0f); // Reset pan for reuse
        candidate->voice->note_on(note_to_freq(note));
    }
}

void VoiceManager::note_on_panned(int note, float velocity, float pan) {
    // Check re-trigger
    for (auto& slot : voices_) {
        if (slot.active && slot.current_note == note) {
            slot.last_note_on_time = next_timestamp();
            slot.voice->set_pan(pan);
            slot.voice->note_on(note_to_freq(note));
            return;
        }
    }

    // Find idle
    for (auto& slot : voices_) {
        if (!slot.voice->is_active()) {
            slot.current_note = note;
            slot.active = true;
            slot.last_note_on_time = next_timestamp();
            slot.voice->set_pan(pan);
            slot.voice->note_on(note_to_freq(note));
            return;
        }
    }

    // 3. Voice Stealing: Priority (Idle > Releasing > Oldest Active)
    VoiceSlot* candidate = nullptr;
    
    // Priority 1: Find Releasing Voice
    for (auto& slot : voices_) {
        if (slot.voice->envelope().is_releasing()) {
            candidate = &slot;
            break;
        }
    }

    // Priority 2: Steal Oldest Active Voice
    if (!candidate) {
        uint64_t oldest_time = std::numeric_limits<uint64_t>::max();
        for (auto& slot : voices_) {
            if (slot.last_note_on_time < oldest_time) {
                oldest_time = slot.last_note_on_time;
                candidate = &slot;
            }
        }
    }

    if (candidate) {
        candidate->current_note = note;
        candidate->active = true;
        candidate->last_note_on_time = next_timestamp();
        candidate->voice->reset();
        candidate->voice->set_pan(pan);
        candidate->voice->note_on(note_to_freq(note));
    }
}

void VoiceManager::set_note_pan(int note, float pan) {
    for (auto& slot : voices_) {
        if (slot.active && slot.current_note == note) {
            slot.voice->set_pan(pan);
        }
    }
}

void VoiceManager::note_off(int note) {
    for (auto& slot : voices_) {
        if (slot.active && slot.current_note == note) {
            slot.voice->note_off();
            // slot.active stays true until envelope finishes in do_pull
        }
    }
}

void VoiceManager::reset() {
    for (auto& slot : voices_) {
        slot.voice->reset();
        slot.current_note = -1;
        slot.active = false;
        slot.last_note_on_time = 0;
    }
    timestamp_counter_ = 0;
}

void VoiceManager::do_pull(std::span<float> output, const VoiceContext* context) {
    std::fill(output.begin(), output.end(), 0.0f);

    // Borrow a stereo block for summation
    // We only need the left channel for mono output summation
    auto block = voices_[0].voice->borrow_buffer();
    std::span<float> voice_span(block->left.data(), output.size());

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
    
    // Master gain adjusted for polyphony
    for (auto& sample : output) {
        sample *= 0.2f;
    }
}

void VoiceManager::do_pull(AudioBuffer& output, const VoiceContext* context) {
    output.clear();

    // Borrow a stereo block from the pool
    auto block = voices_[0].voice->borrow_buffer();
    
    AudioBuffer voice_buf;
    voice_buf.left = std::span<float>(block->left.data(), output.frames());
    voice_buf.right = std::span<float>(block->right.data(), output.frames());

    for (auto& slot : voices_) {
        if (slot.active) {
            if (slot.voice->is_active()) {
                voice_buf.clear();
                slot.voice->pull(voice_buf, context);
                
                for (size_t i = 0; i < output.frames(); ++i) {
                    output.left[i] += voice_buf.left[i];
                    output.right[i] += voice_buf.right[i];
                }
            } else {
                slot.active = false;
                slot.current_note = -1;
            }
        }
    }
    
    // Master gain adjusted for polyphony
    for (size_t i = 0; i < output.frames(); ++i) {
        output.left[i] *= 0.2f;
        output.right[i] *= 0.2f;
    }
}

double VoiceManager::note_to_freq(int note) const {
    return 440.0 * std::pow(2.0, (note - 69) / 12.0);
}

} // namespace audio
