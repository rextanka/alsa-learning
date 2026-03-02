/**
 * @file VoiceManager.cpp
 * @brief Implementation of the VoiceManager class with Flexible Topology support.
 */

#include "VoiceManager.hpp"
#include "Logger.hpp"
#include "envelope/EnvelopeProcessor.hpp"
#include "envelope/AdsrEnvelopeProcessor.hpp"
#include "oscillator/SawtoothOscillatorProcessor.hpp"
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
{
    for (auto& slot : voices_) {
        slot.voice = std::make_unique<Voice>(sample_rate);
        
        // 1. VCO: Sawtooth by default
        slot.voice->add_processor(std::make_unique<SawtoothOscillatorProcessor>(sample_rate), "VCO");
        
        // 2. VCA: ADSR (Audible by default)
        auto adsr = std::make_unique<AdsrEnvelopeProcessor>(sample_rate);
        adsr->set_sustain_level(1.0f);
        slot.voice->add_processor(std::move(adsr), "VCA");
        
        // --- Legacy Parameter Mapping Bridge ---
        
        // Filter mappings (VCF tag expected in future patches)
        slot.voice->register_parameter(1, "VCF", 1); // Cutoff
        slot.voice->register_parameter(2, "VCF", 2); // Resonance
        slot.voice->register_parameter(3, "VCF", 3); // Env Amount
        
        // Envelope stage mappings (VCA tag)
        slot.voice->register_parameter(4, "VCA", 1); // Attack
        slot.voice->register_parameter(5, "VCA", 2); // Decay
        slot.voice->register_parameter(6, "VCA", 3); // Sustain
        slot.voice->register_parameter(7, "VCA", 4); // Release
        
        // Oscillator mappings (VCO tag)
        slot.voice->register_parameter(11, "VCO", 11); // Saw Gain
        slot.voice->register_parameter(12, "VCO", 12); // Pulse Gain
        slot.voice->register_parameter(13, "VCO", 13); // Sub Gain
        slot.voice->register_parameter(14, "VCO", 14); // Pulse Width
        
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

void VoiceManager::note_on(int note, float velocity, double frequency, bool is_virtual_setup) {
    double freq = (frequency > 0.0) ? frequency : note_to_freq(note);

    // 1. Check if the note is already playing (re-trigger)
    int existing_voice_idx = note_to_voice_map_[note & 0x7F];
    if (existing_voice_idx != -1) {
        auto& slot = voices_[existing_voice_idx];
        if (slot.active && slot.current_note == note) {
            slot.last_note_on_time = next_timestamp();
            if (!is_virtual_setup) slot.voice->note_on(freq);
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
        
        float pan_pos = (idle_idx % 2 == 0) ? -1.0f : 1.0f;
        slot.voice->set_pan(pan_pos * voice_spread_);
        
        if (!is_virtual_setup) slot.voice->note_on(freq);
        return;
    }

    // 3. Voice Stealing: Priority (Idle > Releasing > Oldest Active)
    int candidate_idx = -1;
    
    // Priority 1: Find Releasing Voice
    for (int i = 0; i < MAX_VOICES; ++i) {
        if (voices_[i].voice->is_releasing()) {
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
        
        candidate.voice->reset(); 

        if (candidate.current_note != -1) {
            note_to_voice_map_[candidate.current_note & 0x7F] = -1;
        }

        candidate.current_note = note;
        candidate.active = true;
        candidate.last_note_on_time = next_timestamp();
        note_to_voice_map_[note & 0x7F] = candidate_idx;
        candidate.voice->set_pan(0.0f);
        if (!is_virtual_setup) candidate.voice->note_on(freq);
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
    // Map names to legacy Phase 13 parameter IDs
    int param_id = -1;
    if (name == "vcf_cutoff") param_id = 1;
    else if (name == "vcf_res") param_id = 2;
    else if (name == "vcf_env_amount") param_id = 3;
    else if (name == "amp_attack") param_id = 4;
    else if (name == "amp_decay") param_id = 5;
    else if (name == "amp_sustain") param_id = 6;
    else if (name == "amp_release") param_id = 7;
    else if (name == "osc_pw") param_id = 10;
    else if (name == "saw_gain") param_id = 11;
    else if (name == "pulse_gain") param_id = 12;
    else if (name == "sub_gain") param_id = 13;
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

    // 2. Sum active voices
    // We use scratch buffer 0 from the first voice as a temporary summing buffer
    auto voice_span = voices_[0].voice->get_scratch_buffer(0);
    if (voice_span.size() < output.size()) {
        // Fallback or error
        return;
    }
    std::span<float> work_span = voice_span.subspan(0, output.size());

    for (int i = 0; i < MAX_VOICES; ++i) {
        auto& slot = voices_[i];
        if (slot.active) {
            if (slot.voice->is_active()) {
                std::fill(work_span.begin(), work_span.end(), 0.0f);
                slot.voice->pull(work_span, context);
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
    float peak = 0.0f;
    for (auto& sample : output) {
        sample *= 0.15f;
        if (sample > 0.95f) sample = 0.95f + 0.05f * std::tanh((sample - 0.95f) / 0.05f);
        else if (sample < -0.95f) sample = -0.95f + 0.05f * std::tanh((sample + 0.95f) / 0.05f);
        
        float abs_s = std::abs(sample);
        if (abs_s > peak) peak = abs_s;
    }

    if (peak > 0.0001f) {
        static int log_throttle = 0;
        if (log_throttle++ % 100 == 0) {
            char buf[64];
            std::snprintf(buf, sizeof(buf), "Master Peak: %.4f", peak);
            AudioLogger::instance().log_message("VMAN", buf);
        }
    }
}

void VoiceManager::do_pull(AudioBuffer& output, const VoiceContext* context) {
    output.clear();

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

    // 3. Render and Sum Voices with Constant-Power Panning
    auto voice_span = voices_[0].voice->get_scratch_buffer(0);
    if (voice_span.size() < output.frames()) return;
    std::span<float> work_span = voice_span.subspan(0, output.frames());

    for (int i = 0; i < MAX_VOICES; ++i) {
        auto& slot = voices_[i];
        if (slot.active) {
            if (slot.voice->is_active()) {
                std::fill(work_span.begin(), work_span.end(), 0.0f);
                slot.voice->pull(work_span, context);

                float pan = slot.voice->pan();
                float theta = (pan + 1.0f) * (static_cast<float>(M_PI) / 4.0f);
                float gainL = std::cos(theta);
                float gainR = std::sin(theta);

                for (size_t j = 0; j < output.frames(); ++j) {
                    output.left[j] += work_span[j] * gainL;
                    output.right[j] += work_span[j] * gainR;
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
    
    // 4. Master Safety Gain (0.15) and Simple Soft Clipping
    for (size_t j = 0; j < output.frames(); ++j) {
        float outL = output.left[j] * 0.15f;
        float outR = output.right[j] * 0.15f;

        auto soft_clip = [](float s) {
            if (s > 0.95f) return 0.95f + 0.05f * std::tanh((s - 0.95f) / 0.05f);
            if (s < -0.95f) return -0.95f + 0.05f * std::tanh((s + 0.95f) / 0.05f);
            return s;
        };

        output.left[j] = soft_clip(outL);
        output.right[j] = soft_clip(outR);
    }
}

double VoiceManager::note_to_freq(int note) const {
    return 440.0 * std::pow(2.0, (note - 69) / 12.0);
}

} // namespace audio
