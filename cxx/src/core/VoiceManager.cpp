/**
 * @file VoiceManager.cpp
 * @brief Implementation of the VoiceManager class with advanced management.
 */

#include "VoiceManager.hpp"
#include "Logger.hpp"
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
    note_to_voice_map_.fill(-1);
}

void VoiceManager::note_on(int note, float velocity, double frequency) {
    double freq = (frequency > 0.0) ? frequency : note_to_freq(note);

    // 1. Check if the note is already playing (re-trigger)
    int existing_voice_idx = note_to_voice_map_[note & 0x7F];
    if (existing_voice_idx != -1) {
        auto& slot = voices_[existing_voice_idx];
        if (slot.active && slot.current_note == note) {
            slot.last_note_on_time = next_timestamp();
            slot.voice->note_on(freq);
            return;
        }
    }

    // 2. Find an idle voice
    int idle_idx = -1;
    for (int i = 0; i < MAX_VOICES; ++i) {
        if (!voices_[i].voice->is_active()) {
            idle_idx = i;
            break;
        }
    }

    if (idle_idx != -1) {
        auto& slot = voices_[idle_idx];
        slot.current_note = note;
        slot.active = true;
        slot.last_note_on_time = next_timestamp();
        note_to_voice_map_[note & 0x7F] = idle_idx;
        slot.voice->note_on(freq);
        return;
    }

    // 3. Voice Stealing: Priority (Idle > Releasing > Oldest Active)
    int candidate_idx = -1;
    
    // Priority 1: Find Releasing Voice
    for (int i = 0; i < MAX_VOICES; ++i) {
        if (voices_[i].voice->envelope().is_releasing()) {
            candidate_idx = i;
            break;
        }
    }

    // Priority 2: Steal Oldest Active Voice
    if (candidate_idx == -1) {
        uint64_t oldest_time = std::numeric_limits<uint64_t>::max();
        for (int i = 0; i < MAX_VOICES; ++i) {
            if (voices_[i].last_note_on_time < oldest_time) {
                oldest_time = voices_[i].last_note_on_time;
                candidate_idx = i;
            }
        }
    }

    if (candidate_idx != -1) {
        auto& candidate = voices_[candidate_idx];
        AudioLogger::instance().log_event("VoiceSteal", static_cast<float>(candidate.current_note));
        
        // Clear old mapping if it exists
        if (candidate.current_note != -1) {
            note_to_voice_map_[candidate.current_note & 0x7F] = -1;
        }

        candidate.current_note = note;
        candidate.active = true;
        candidate.last_note_on_time = next_timestamp();
        note_to_voice_map_[note & 0x7F] = candidate_idx;
        candidate.voice->reset(); // Avoid artifacts
        candidate.voice->set_pan(0.0f); // Reset pan for reuse
        candidate.voice->note_on(freq);
    }
}

void VoiceManager::note_on_panned(int note, float velocity, float pan) {
    note_on(note, velocity);
    set_note_pan(note, pan);
}

void VoiceManager::set_note_pan(int note, float pan) {
    int voice_idx = note_to_voice_map_[note & 0x7F];
    if (voice_idx != -1) {
        auto& slot = voices_[voice_idx];
        if (slot.active && slot.current_note == note) {
            slot.voice->set_pan(pan);
        }
    }
}

void VoiceManager::note_off(int note) {
    int voice_idx = note_to_voice_map_[note & 0x7F];
    if (voice_idx != -1) {
        auto& slot = voices_[voice_idx];
        if (slot.active && slot.current_note == note) {
            slot.voice->note_off();
            note_to_voice_map_[note & 0x7F] = -1;
        }
    }
}

void VoiceManager::handleMidiEvent(const MidiEvent& event) {
    if (event.isNoteOn()) {
        note_on(event.data1, event.data2 / 127.0f);
    } else if (event.isNoteOff()) {
        note_off(event.data1);
    }
}

void VoiceManager::reset() {
    for (auto& slot : voices_) {
        slot.voice->reset();
        slot.current_note = -1;
        slot.active = false;
        slot.last_note_on_time = 0;
    }
    note_to_voice_map_.fill(-1);
    timestamp_counter_ = 0;
}

void VoiceManager::do_pull(std::span<float> output, const VoiceContext* context) {
    std::fill(output.begin(), output.end(), 0.0f);

    auto block = voices_[0].voice->borrow_buffer();
    std::span<float> voice_span(block->left.data(), output.size());

    for (int i = 0; i < MAX_VOICES; ++i) {
        auto& slot = voices_[i];
        if (slot.active) {
            if (slot.voice->is_active()) {
                slot.voice->pull(voice_span, context);
                for (size_t j = 0; j < output.size(); ++j) {
                    output[j] += voice_span[j];
                }
            } else {
                slot.active = false;
                if (slot.current_note != -1) {
                    // Only clear the map if it still points to this voice
                    if (note_to_voice_map_[slot.current_note & 0x7F] == i) {
                        note_to_voice_map_[slot.current_note & 0x7F] = -1;
                    }
                }
                slot.current_note = -1;
            }
        }
    }
    
    for (auto& sample : output) {
        sample *= 0.4f; // Safety factor
    }
}

void VoiceManager::do_pull(AudioBuffer& output, const VoiceContext* context) {
    output.clear();

    auto block = voices_[0].voice->borrow_buffer();
    
    AudioBuffer voice_buf;
    voice_buf.left = std::span<float>(block->left.data(), output.frames());
    voice_buf.right = std::span<float>(block->right.data(), output.frames());

    for (int i = 0; i < MAX_VOICES; ++i) {
        auto& slot = voices_[i];
        if (slot.active) {
            if (slot.voice->is_active()) {
                voice_buf.clear();
                slot.voice->pull(voice_buf, context);
                
                for (size_t j = 0; j < output.frames(); ++j) {
                    output.left[j] += voice_buf.left[j];
                    output.right[j] += voice_buf.right[j];
                }
            } else {
                slot.active = false;
                if (slot.current_note != -1) {
                    if (note_to_voice_map_[slot.current_note & 0x7F] == i) {
                        note_to_voice_map_[slot.current_note & 0x7F] = -1;
                    }
                }
                slot.current_note = -1;
            }
        }
    }
    
    for (size_t j = 0; j < output.frames(); ++j) {
        output.left[j] *= 0.2f;
        output.right[j] *= 0.2f;
    }
}

double VoiceManager::note_to_freq(int note) const {
    return 440.0 * std::pow(2.0, (note - 69) / 12.0);
}

} // namespace audio
