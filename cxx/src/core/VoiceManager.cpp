/**
 * @file VoiceManager.cpp 
 * @brief Implementation of the VoiceManager class.
 */

#include "VoiceManager.hpp"
#include "SummingBus.hpp"
#include "AudioTap.hpp"
#include "Logger.hpp"
#include <cmath>
#include <algorithm>
#include <limits>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace audio {

VoiceManager::VoiceManager(int sample_rate)
    : timestamp_counter_(0)
    , sample_rate_(sample_rate)
    , summing_bus_(std::make_unique<SummingBus>(512))
{
    // Voices start empty — signal chain is built by engine_bake() via rebuild_all_voices().
    for (auto& slot : voices_) {
        slot.voice = std::make_unique<Voice>(sample_rate);
        slot.current_note = -1;
        slot.active = false;
        slot.last_note_on_time = 0;
    }
    note_to_voice_map_.fill(-1);
}

void VoiceManager::rebuild_all_voices(const std::function<std::unique_ptr<Voice>()>& factory) {
    for (auto& slot : voices_) {
        slot.voice         = factory();
        slot.current_note  = -1;
        slot.active        = false;
        slot.last_note_on_time = 0;
    }
    note_to_voice_map_.fill(-1);
}

void VoiceManager::set_voice_spread(float spread) {
    voice_spread_ = std::clamp(spread, 0.0f, 1.0f);
}

void VoiceManager::note_on(int note, float /* velocity */, double frequency) {
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
        AudioLogger::instance().log_event("VoiceAssign", static_cast<float>(note));
        auto& slot = voices_[idle_idx];
        slot.current_note = note;
        slot.active = true;
        slot.last_note_on_time = next_timestamp();
        note_to_voice_map_[note & 0x7F] = idle_idx;
        
        float pan_pos = (idle_idx % 2 == 0) ? -1.0f : 1.0f;
        slot.voice->set_pan(pan_pos * voice_spread_);
        
        slot.voice->note_on(freq);
        return;
    }

    // 3. Voice Stealing: Tiered Priority (Releasing > Oldest Active)
    int candidate_idx = -1;
    uint64_t oldest_releasing_time = std::numeric_limits<uint64_t>::max();
    uint64_t oldest_active_time = std::numeric_limits<uint64_t>::max();
    int oldest_active_idx = -1;

    for (int i = 0; i < MAX_VOICES; ++i) {
        if (voices_[i].voice->is_releasing()) {
            if (voices_[i].last_note_on_time < oldest_releasing_time) {
                oldest_releasing_time = voices_[i].last_note_on_time;
                candidate_idx = i;
            }
        } else {
            if (voices_[i].last_note_on_time < oldest_active_time) {
                oldest_active_time = voices_[i].last_note_on_time;
                oldest_active_idx = i;
            }
        }
    }

    // If no releasing voice found, fallback to oldest active
    if (candidate_idx == -1) {
        candidate_idx = oldest_active_idx;
    }

    if (candidate_idx != -1) {
        AudioLogger::instance().log_message("VoiceSteal", "stealing");
        auto& candidate = voices_[candidate_idx];
        AudioLogger::instance().log_event("VoiceSteal", static_cast<float>(candidate.current_note));
        
        candidate.voice->reset(); 

        if (candidate.current_note != -1) {
            note_to_voice_map_[candidate.current_note & 0x7F] = -1;
        }

        candidate.current_note = note;
        candidate.active = true;
        candidate.last_note_on_time = next_timestamp();
        note_to_voice_map_[note & 0x7F] = candidate_idx;
        candidate.voice->set_pan(0.0f);
        candidate.voice->note_on(freq);
    }
}

void VoiceManager::note_on(int note, float /* velocity */, int group_id, double frequency) {
    double freq = (frequency > 0.0) ? frequency : note_to_freq(note);

    // Re-trigger check within group
    int existing_idx = note_to_voice_map_[note & 0x7F];
    if (existing_idx != -1) {
        auto& slot = voices_[existing_idx];
        if (slot.active && slot.current_note == note && slot.group_id == group_id) {
            slot.last_note_on_time = next_timestamp();
            slot.voice->note_on(freq);
            return;
        }
    }

    // Find idle voice in group
    int idle_idx = -1;
    for (int i = 0; i < MAX_VOICES; ++i) {
        if (voices_[i].group_id == group_id && !voices_[i].voice->is_active()) {
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
        slot.voice->set_pan((idle_idx % 2 == 0) ? -1.0f * voice_spread_ : voice_spread_);
        slot.voice->note_on(freq);
        return;
    }

    // Voice stealing within group
    int candidate_idx = -1;
    uint64_t oldest_releasing = std::numeric_limits<uint64_t>::max();
    uint64_t oldest_active   = std::numeric_limits<uint64_t>::max();
    int oldest_active_idx = -1;

    for (int i = 0; i < MAX_VOICES; ++i) {
        if (voices_[i].group_id != group_id) continue;
        if (voices_[i].voice->is_releasing()) {
            if (voices_[i].last_note_on_time < oldest_releasing) {
                oldest_releasing = voices_[i].last_note_on_time;
                candidate_idx = i;
            }
        } else {
            if (voices_[i].last_note_on_time < oldest_active) {
                oldest_active = voices_[i].last_note_on_time;
                oldest_active_idx = i;
            }
        }
    }
    if (candidate_idx == -1) candidate_idx = oldest_active_idx;

    if (candidate_idx != -1) {
        auto& candidate = voices_[candidate_idx];
        if (candidate.current_note != -1) note_to_voice_map_[candidate.current_note & 0x7F] = -1;
        candidate.voice->reset();
        candidate.current_note = note;
        candidate.active = true;
        candidate.last_note_on_time = next_timestamp();
        note_to_voice_map_[note & 0x7F] = candidate_idx;
        candidate.voice->set_pan(0.0f);
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

void VoiceManager::set_tag_parameter(const std::string& tag, const std::string& name, float value) {
    for (auto& slot : voices_) {
        if (slot.voice) slot.voice->set_tag_parameter(tag, name, value);
    }
}

void VoiceManager::set_tag_string_parameter(const std::string& tag, const std::string& name,
                                             const std::string& value) {
    for (auto& slot : voices_)
        if (slot.voice) slot.voice->set_tag_string_parameter(tag, name, value);
}

void VoiceManager::flush_all_processors() {
    for (auto& slot : voices_)
        if (slot.voice) slot.voice->flush_all_processors();
}

void VoiceManager::set_parameter_by_name(const std::string& name, float value) {
    for (auto& slot : voices_) {
        if (slot.voice) slot.voice->set_named_parameter(name, value);
    }
}

void VoiceManager::set_group_parameter(int group_id, const std::string& name, float value) {
    for (auto& slot : voices_) {
        if (slot.voice && slot.group_id == group_id)
            slot.voice->set_named_parameter(name, value);
    }
}


void VoiceManager::assign_group(int voice_idx, int group_id) {
    if (voice_idx >= 0 && voice_idx < MAX_VOICES) {
        voices_[voice_idx].group_id = group_id;
    }
}

void VoiceManager::handleMidiEvent(const MidiEvent& event) {
    if (event.isNoteOn()) {
        note_on(event.data1, event.data2 / 127.0f);
    } else if (event.isNoteOff()) {
        note_off(event.data1);
    }
}

void VoiceManager::processMidiBytes(const uint8_t* data, size_t size, uint32_t sampleOffset) {
    midi_parser_.parse(data, size, sampleOffset, [this](const MidiEvent& event) {
        handleMidiEvent(event);
    });
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
    // Mono Summing for mono-span output
    std::fill(output.begin(), output.end(), 0.0f);

    // Sum active voices into mono span
    auto block = voices_[0].voice->borrow_buffer();
    std::span<float> work_span(block->left.data(), output.size());

    for (int i = 0; i < MAX_VOICES; ++i) {
        auto& slot = voices_[i];
        if (slot.active) {
            if (slot.voice->is_active()) {
                std::fill(work_span.begin(), work_span.end(), 0.0f);
                slot.voice->pull_mono(work_span, context);
                for (size_t j = 0; j < output.size(); ++j) {
                    output[j] += work_span[j];
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
    
    // Master Safety Gain (0.15) and Simple Soft Clipping
    for (auto& sample : output) {
        sample *= 0.15f;
        if (sample > 0.95f) sample = 0.95f + 0.05f * std::tanh((sample - 0.95f) / 0.05f);
        else if (sample < -0.95f) sample = -0.95f + 0.05f * std::tanh((sample + 0.95f) / 0.05f);
    }
}

void VoiceManager::do_pull(AudioBuffer& output, const VoiceContext* context) {
    pull_with_tap(output, diagnostic_tap_, context);
}

void VoiceManager::pull_with_tap(AudioBuffer& output, Processor* tap, const VoiceContext* context) {
    output.clear();
    summing_bus_->clear();

    // Render and Sum Voices into SummingBus
    auto block = voices_[0].voice->borrow_buffer();
    std::span<float> work_span(block->left.data(), output.frames());

    int active_slots = 0;
    for (int i = 0; i < MAX_VOICES; ++i) {
        auto& slot = voices_[i];
        if (slot.active) {
            if (slot.voice->is_active()) {
                active_slots++;
                std::fill(work_span.begin(), work_span.end(), 0.0f);
                slot.voice->pull_mono(work_span, context);

                // Feed diagnostic tap if provided (non-destructive push)
                if (tap) {
                    if (auto* audio_tap = dynamic_cast<AudioTap*>(tap)) {
                        audio_tap->write(work_span);
                    }
                }

                summing_bus_->add_voice(work_span, slot.voice->pan());
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
    
    summing_bus_->pull(output);

    // RT-SAFE: periodic telemetry via lock-free AudioLogger
    static int call_count = 0;
    if (call_count++ % sample_rate_ == 0) {
        AudioLogger::instance().log_event("ActiveVoices", static_cast<float>(active_slots));
    }
}

double VoiceManager::note_to_freq(int note) const {
    return 440.0 * std::pow(2.0, (note - 69) / 12.0);
}

} // namespace audio
