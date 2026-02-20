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
{
    oscillator_ = std::make_unique<WavetableOscillatorProcessor>(static_cast<double>(sample_rate));
    envelope_ = std::make_unique<AdsrEnvelopeProcessor>(sample_rate);
    // Default to Moog filter
    filter_ = std::make_unique<MoogLadderProcessor>(sample_rate);
    filter_->set_cutoff(20000.0f); // Default open
}

void Voice::set_filter_type(std::unique_ptr<FilterProcessor> filter) {
    filter_ = std::move(filter);
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
    // 1. OSCILLATOR -> output
    oscillator_->pull(output, context);

    // 2. FILTER -> output
    if (filter_) {
        filter_->pull(output, context);
    }

    // 3. VCA (ADSR) -> output
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

} // namespace audio
