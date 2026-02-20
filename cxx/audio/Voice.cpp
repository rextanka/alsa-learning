/**
 * @file Voice.cpp
 * @brief Implementation of the Voice class.
 */

#include "Voice.hpp"
#include <vector>

namespace audio {

Voice::Voice(int sample_rate)
    : sample_rate_(sample_rate)
{
    oscillator_ = std::make_unique<WavetableOscillatorProcessor>(static_cast<double>(sample_rate));
    envelope_ = std::make_unique<AdsrEnvelopeProcessor>(sample_rate);
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
}

void Voice::do_pull(std::span<float> output, const VoiceContext* context) {
    // 1. Pull audio from the oscillator into the output span
    oscillator_->pull(output, context);

    // 2. Multiply each sample by the envelope value
    // We need to process the envelope sample-by-sample to apply it correctly to the buffer.
    // However, AdsrEnvelopeProcessor also implements Processor, so we can pull from it.
    
    // Create a temporary buffer for envelope values
    // Using a stack-based buffer if possible or a small vector.
    // Given we are in do_pull, we should avoid large allocations.
    
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
