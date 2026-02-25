/**
 * @file Voice.cpp
 * @brief Implementation of the Voice class for the British Church Organ timbre.
 */

#include "Voice.hpp"
#include "oscillator/SquareOscillatorProcessor.hpp"
#include "filter/MoogLadderProcessor.hpp"
#include <vector>
#include <cmath>

namespace audio {

Voice::Voice(int sample_rate)
    : sample_rate_(sample_rate)
    , pan_(0.0f) // Center
{
    // 1. Oscillator: 50% Pulse (Square Wave) for pipe organ aesthetic
    oscillator_ = std::make_unique<SquareOscillatorProcessor>(static_cast<double>(sample_rate));
    
    // 2. ADSR: British Church Organ settings
    envelope_ = std::make_unique<AdsrEnvelopeProcessor>(sample_rate);
    envelope_->set_attack_time(0.015f);  // 15ms Attack: Air pressure building
    envelope_->set_decay_time(0.001f);   // Organs don't decay (min 1ms)
    envelope_->set_sustain_level(1.0f);  // Full volume
    envelope_->set_release_time(0.050f); // 50ms Release: Valve closure & initial reflection

    // 3. Filter: Moog ladder with moderate resonance for "chiff"
    filter_ = std::make_unique<MoogLadderProcessor>(sample_rate);
    filter_->set_cutoff(4000.0f); // Moderate open for pipe character
    filter_->set_resonance(0.4f); // Increased resonance for audible chiff

    graph_ = std::make_unique<AudioGraph>();
    rebuild_graph();
}

void Voice::set_filter_type(std::unique_ptr<FilterProcessor> filter) {
    filter_ = std::move(filter);
    rebuild_graph();
}

void Voice::set_pan(float pan) {
    pan_ = std::clamp(pan, -1.0f, 1.0f);
}

void Voice::rebuild_graph() {
    graph_->clear();
    graph_->add_node(oscillator_.get());
    if (filter_) {
        graph_->add_node(filter_.get());
    }
}

void Voice::note_on(double frequency) {
    oscillator_->set_frequency(frequency);
    envelope_->gate_on();
    
    if (filter_) filter_->reset();
}

void Voice::note_off() {
    envelope_->gate_off();
}

bool Voice::is_active() const {
    return envelope_->is_active();
}

void Voice::reset() {
    oscillator_->reset();
    envelope_->reset();
    if (filter_) filter_->reset();
}

void Voice::do_pull(std::span<float> output, const VoiceContext* context) {
    // 1. Process the graph (Oscillator -> Filter)
    graph_->pull(output, context);

    // 2. Apply VCA (ADSR) & "Chiff" Filter Modulation
    static thread_local std::vector<float> envelope_buffer;
    if (envelope_buffer.size() < output.size()) {
        envelope_buffer.resize(output.size());
    }
    
    std::span<float> env_span(envelope_buffer.data(), output.size());
    envelope_->pull(env_span, context);
    
    for (size_t i = 0; i < output.size(); ++i) {
        float gain = env_span[i];
        
        // Apply "Chiff": Modulate filter cutoff based on envelope
        // Scale: Base Cutoff (4000Hz) + (Envelope * 2000Hz)
        if (filter_) {
            filter_->set_cutoff(4000.0f + (gain * 2000.0f));
        }

        output[i] *= gain;
    }
}

void Voice::do_pull(AudioBuffer& output, const VoiceContext* context) {
    // 1. Process the graph (Oscillator -> Filter)
    graph_->pull(output, context);

    // 2. Apply VCA (ADSR) & "Chiff" Filter Modulation
    static thread_local std::vector<float> envelope_buffer;
    if (envelope_buffer.size() < output.frames()) {
        envelope_buffer.resize(output.frames());
    }
    
    std::span<float> env_span(envelope_buffer.data(), output.frames());
    envelope_->pull(env_span, context);
    
    // Constant power panning approximation
    float pan_rad = (pan_ + 1.0f) * (M_PI / 4.0f);
    float gain_l = std::cos(pan_rad);
    float gain_r = std::sin(pan_rad);

    for (size_t i = 0; i < output.frames(); ++i) {
        float gain = env_span[i];
        
        if (filter_) {
            filter_->set_cutoff(4000.0f + (gain * 2000.0f));
        }

        output.left[i] *= (gain * gain_l);
        output.right[i] *= (gain * gain_r);
    }
}

} // namespace audio
