/**
 * @file VoiceManager.cpp 
 * @brief Implementation of the VoiceManager class.
 */

#include "VoiceManager.hpp"
#include "VoiceFactory.hpp"
#include "SummingBus.hpp"
#include "AudioTap.hpp"
#include "Logger.hpp"
#include <cmath>
#include <algorithm>
#include <limits>
#include <iostream>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace audio {

VoiceManager::VoiceManager(int sample_rate)
    : timestamp_counter_(0)
    , sample_rate_(sample_rate)
    , summing_bus_(std::make_unique<SummingBus>(512))
{
    for (auto& slot : voices_) {
        slot.voice = VoiceFactory::createSH101(sample_rate);
        
        // Initial modulation state is clean. Primary VCA gate is handled
        // by Voice::pull_mono calling envelope_->pull() directly.
        
        slot.current_note = -1;
        slot.active = false;
        slot.last_note_on_time = 0;
    }
    note_to_voice_map_.fill(-1);
    mod_buffer_.resize(512); // Default block size
}

void VoiceManager::set_voice_spread(float spread) {
    voice_spread_ = std::clamp(spread, 0.0f, 1.0f);
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
        std::cout << "[DEBUG] VoiceManager::note_on assigning idle voice " << idle_idx << " for note " << note << std::endl;
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
        std::cout << "[DEBUG] VoiceManager::note_on stealing voice " << candidate_idx << " for note " << note << std::endl;
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

void VoiceManager::set_parameter_by_name(const std::string& name, float value) {
    // Map names to Phase 13 Global IDs
    int param_id = -1;
    if (name == "vcf_cutoff") param_id = 1;
    else if (name == "vcf_res") param_id = 2;
    else if (name == "vcf_env_amount") param_id = 3;
    else if (name == "amp_attack") param_id = 4;
    else if (name == "amp_decay") param_id = 5;
    else if (name == "amp_sustain") param_id = 6;
    else if (name == "amp_release") param_id = 7;
    else if (name == "osc_pw") param_id = 10;
    else if (name == "sub_gain") param_id = 11;
    else if (name == "saw_gain") param_id = 12;
    else if (name == "pulse_gain") param_id = 13;
    else if (name == "pulse_width") param_id = 14;

    if (param_id != -1) {
        set_parameter(param_id, value);
    }
}

void VoiceManager::set_parameter(int param_id, float value) {
    for (auto& slot : voices_) {
        if (slot.voice) {
            slot.voice->set_parameter(param_id, value);
        }
    }
}

void VoiceManager::set_filter_type(int type) {
    for (auto& slot : voices_) {
        if (slot.voice) {
            slot.voice->set_filter_type(type);
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
    for (auto& [id, source] : mod_sources_) {
        source->reset();
    }
}

void VoiceManager::clear_connections(int id) {
    connections_.erase(
        std::remove_if(connections_.begin(), connections_.end(),
            [id](const Connection& c) { return c.source_id == id || c.target_id == id; }),
        connections_.end());
    mod_sources_.erase(id);
}

void VoiceManager::add_connection(int source_id, int target_id, int param, float intensity) {
    connections_.push_back({source_id, target_id, param, intensity});
}

void VoiceManager::set_mod_source(int id, std::shared_ptr<Processor> processor) {
    mod_sources_[id] = std::move(processor);
}

void VoiceManager::do_pull(std::span<float> output, const VoiceContext* context) {
    // Mono Summing for mono-span output
    std::fill(output.begin(), output.end(), 0.0f);

    if (mod_buffer_.size() < output.size()) {
        mod_buffer_.resize(output.size());
    }

    // 1. Process modulation links
    for (const auto& conn : connections_) {
        auto it = mod_sources_.find(conn.source_id);
        if (it != mod_sources_.end()) {
            std::span<float> mod_span(mod_buffer_.data(), output.size());
            it->second->pull(mod_span, context);
            
            float mod_value = mod_span[0]; // Block-rate: use first value
            float applied_val = mod_value * conn.intensity;

            if (conn.target_id == -1) { // ALL_VOICES
                for (int i = 0; i < MAX_VOICES; ++i) {
                    voices_[i].voice->set_parameter(conn.param, applied_val);
                }
            }
        }
    }

    // 2. Sum active voices into mono span
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

    if (mod_buffer_.size() < output.frames()) {
        mod_buffer_.resize(output.frames());
    }

    // 2. Process global modulation links
    for (const auto& conn : connections_) {
        auto it = mod_sources_.find(conn.source_id);
        if (it != mod_sources_.end()) {
            std::span<float> mod_span(mod_buffer_.data(), output.frames());
            it->second->pull(mod_span, context);
            
            float mod_value = mod_span[0];
            float applied_val = mod_value * conn.intensity;

            if (conn.target_id == -1) { // ALL_VOICES
                for (int i = 0; i < MAX_VOICES; ++i) {
                    voices_[i].voice->set_parameter(conn.param, applied_val);
                }
            }
        }
    }

    // 3. Render and Sum Voices into SummingBus
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
    
    // 4. Final Output from Bus
    summing_bus_->pull(output);

    static int call_count = 0;
    if (call_count++ % sample_rate_ == 0) {
        std::cout << "[DEBUG] VoiceManager::do_pull active_slots=" << active_slots << std::endl;
    }
}

double VoiceManager::note_to_freq(int note) const {
    return 440.0 * std::pow(2.0, (note - 69) / 12.0);
}

} // namespace audio
