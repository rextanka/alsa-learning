/**
 * @file Voice.cpp
 * @brief Implementation of the Voice class.
 */

#include "Voice.hpp"
#include "filter/MoogLadderProcessor.hpp"
#include <vector>

namespace audio {

Voice::Voice(int sample_rate)
    : sample_rate_(sample_rate)
    , pan_(0.0f) // Center
{
    oscillator_ = std::make_unique<WavetableOscillatorProcessor>(static_cast<double>(sample_rate));
    envelope_ = std::make_unique<AdsrEnvelopeProcessor>(sample_rate);
    // Default to Moog filter
    filter_ = std::make_unique<MoogLadderProcessor>(sample_rate);
    filter_->set_cutoff(20000.0f); // Default open

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
    // Envelope is applied manually for now to control the VCA logic
    // but could also be a node if it processed the signal.
}

void Voice::note_on(double frequency) {
    oscillator_->setFrequency(frequency);
    envelope_->gate_on();
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

    // 2. Apply VCA (ADSR)
    static thread_local std::vector<float> envelope_buffer;
    if (envelope_buffer.size() < output.size()) {
        envelope_buffer.resize(output.size());
    }
    
    std::span<float> env_span(envelope_buffer.data(), output.size());
    envelope_->pull(env_span, context);
    
    for (size_t i = 0; i < output.size(); ++i) {
        output[i] *= env_span[i];
    }
}

void Voice::do_pull(AudioBuffer& output, const VoiceContext* context) {
    // 1. Process the graph (Oscillator -> Filter)
    graph_->pull(output, context);

    // 2. Apply VCA (ADSR)
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
        output.left[i] *= (gain * gain_l);
        output.right[i] *= (gain * gain_r);
    }
}

} // namespace audio
