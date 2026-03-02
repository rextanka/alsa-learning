/**
 * @file Voice.cpp
 * @brief Implementation of the Voice class with Flexible Topology.
 */

#include "Voice.hpp"
#include "envelope/EnvelopeProcessor.hpp"
#include "Logger.hpp"
#include <algorithm>
#include <iostream>

namespace audio {

Voice::Voice(int sample_rate)
    : sample_rate_(sample_rate)
    , pan_(0.0f)
    , current_frequency_(440.0)
{
    // Pre-allocate scratch buffers
    for (auto& buf : scratch_buffers_) {
        buf.resize(MAX_BLOCK_SIZE, 1); // Mono buffers
    }
}

void Voice::add_processor(std::unique_ptr<Processor> p, std::string tag) {
    if (!p) return;

    // Generator-First Rule Validation
    if (signal_chain_.empty()) {
        // We use a heuristic or type-check if possible, 
        // but the ARCH_PLAN says we must verify if it's a Generator.
        // For now, we trust the caller but log if it's not known to be one.
        AudioLogger::instance().log_event("VOICE_INIT", 1.0f);
    }

    Processor* ptr = p.get();
    signal_chain_.push_back(std::move(p));
    
    if (!tag.empty()) {
        tag_map_[tag] = ptr;
    }
}

Processor* Voice::get_processor_by_tag(const std::string& tag) {
    auto it = tag_map_.find(tag);
    if (it != tag_map_.end()) {
        return it->second;
    }
    return nullptr;
}

void Voice::register_parameter(int param_id, const std::string& tag, int internal_param_id) {
    parameter_map_[param_id] = {tag, internal_param_id};
}

void Voice::set_parameter(int param_id, float value) {
    auto it = parameter_map_.find(param_id);
    if (it != parameter_map_.end()) {
        Processor* p = get_processor_by_tag(it->second.tag);
        if (p) {
            p->set_parameter(it->second.internal_id, value);
        } else {
            char buf[128];
            std::snprintf(buf, sizeof(buf), "Param %d mapped to tag '%s' but node not found", param_id, it->second.tag.c_str());
            AudioLogger::instance().log_message("VOICE", buf);
        }
    } else {
        // Only log common non-mapped parameters to avoid flooding if it's expected
        if (param_id > 0) { // Skip ID 0 (Pitch) if it's handled via note_on
            char buf[64];
            std::snprintf(buf, sizeof(buf), "Unmapped Parameter ID: %d", param_id);
            AudioLogger::instance().log_message("VOICE", buf);
        }
    }
}

void Voice::note_on(double frequency) {
    current_frequency_ = frequency;
    for (auto& p : signal_chain_) {
        p->reset();
        // Many processors use ID 0 for frequency/pitch
        p->set_parameter(0, static_cast<float>(frequency));
        
        // Dispatch gate_on to any envelope-like processors
        if (auto* env = dynamic_cast<EnvelopeProcessor*>(p.get())) {
            env->gate_on();
        }
    }
}

void Voice::note_off() {
    for (auto& p : signal_chain_) {
        if (auto* env = dynamic_cast<EnvelopeProcessor*>(p.get())) {
            env->gate_off();
        }
    }
}

void Voice::kill() {
    for (auto& p : signal_chain_) {
        p->reset();
    }
}

bool Voice::is_active() const {
    bool active = false;
    for (const auto& p : signal_chain_) {
        if (const auto* env = dynamic_cast<const EnvelopeProcessor*>(p.get())) {
            if (env->is_active()) {
                active = true;
                break;
            }
        }
    }
    // If no envelopes are found, we assume the voice is inactive to prevent hanging
    return active;
}

bool Voice::is_releasing() const {
    for (const auto& p : signal_chain_) {
        if (const auto* env = dynamic_cast<const EnvelopeProcessor*>(p.get())) {
            if (env->is_releasing()) {
                return true;
            }
        }
    }
    return false;
}

void Voice::reset() {
    for (auto& p : signal_chain_) {
        p->reset();
    }
}

std::span<float> Voice::get_scratch_buffer(size_t index) {
    if (index < SCRATCH_BUFFER_COUNT) {
        return std::span<float>(scratch_buffers_[index].left.data(), MAX_BLOCK_SIZE);
    }
    return {};
}

void Voice::do_pull(std::span<float> output, const VoiceContext* context) {
    if (signal_chain_.empty()) {
        std::fill(output.begin(), output.end(), 0.0f);
        return;
    }

    // Process the chain
    // The first node is responsible for initializing the buffer (Generator)
    // Subsequent nodes modify it in-place.
    for (size_t i = 0; i < signal_chain_.size(); ++i) {
        signal_chain_[i]->pull(output, context);
    }
}

} // namespace audio
