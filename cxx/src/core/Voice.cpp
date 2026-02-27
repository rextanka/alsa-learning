/**
 * @file Voice.cpp
 * @brief Implementation of the Voice class with Modular Modulation Matrix.
 */

#include "Voice.hpp"
#include "oscillator/PulseOscillatorProcessor.hpp"
#include "filter/MoogLadderProcessor.hpp"
#include <vector>
#include <cmath>

namespace audio {

Voice::Voice(int sample_rate)
    : base_frequency_(440.0)
    , base_cutoff_(4000.0f)
    , base_resonance_(0.4f)
    , base_amplitude_(1.0f)
    , sample_rate_(sample_rate)
    , pan_(0.0f)
{
    // 1. Oscillator: Pulse Oscillator (SH-101/Juno Style)
    oscillator_ = std::make_unique<PulseOscillatorProcessor>(sample_rate);
    sub_oscillator_ = std::make_unique<SubOscillator>();
    source_mixer_ = std::make_unique<SourceMixer>();
    source_mixer_->set_gain(1, 1.0f); // Default main pulse gain
    source_mixer_->set_gain(2, 0.5f); // Default sub-osc gain
    
    // 2. ADSR: British Church Organ settings
    envelope_ = std::make_unique<AdsrEnvelopeProcessor>(sample_rate);
    envelope_->set_attack_time(0.015f);
    envelope_->set_decay_time(0.001f);
    envelope_->set_sustain_level(1.0f);
    envelope_->set_release_time(0.050f);

    // 3. Filter: Moog ladder
    filter_ = std::make_unique<MoogLadderProcessor>(sample_rate);
    filter_->set_cutoff(base_cutoff_);
    filter_->set_resonance(base_resonance_);

    // 4. LFO: For Vibrato/Tremolo
    lfo_ = std::make_unique<LfoProcessor>(sample_rate);
    lfo_->set_frequency(5.0);
    lfo_->set_intensity(0.0f); // Default off

    graph_ = std::make_unique<AudioGraph>();
    rebuild_graph();

    // Default "Chiff" Modulation: Envelope -> Cutoff (+0.5 octaves)
    matrix_.set_connection(ModulationSource::Envelope, ModulationTarget::Cutoff, 0.585f);
    
    current_source_values_.fill(0.0f);
}

void Voice::set_filter_type(std::unique_ptr<FilterProcessor> filter) {
    filter_ = std::move(filter);
    rebuild_graph();
}

void Voice::set_pan(float pan) {
    pan_ = std::clamp(pan, -1.0f, 1.0f);
}

void Voice::set_parameter(int param, float value) {
    switch (param) {
        case 0: // PITCH
            base_frequency_ = static_cast<double>(value);
            break;
        case 1: // CUTOFF
            base_cutoff_ = std::max(20.0f, value);
            break;
        case 2: // RESONANCE
            base_resonance_ = std::clamp(value, 0.0f, 1.0f);
            break;
        case 11: // SUB_GAIN
            source_mixer_->set_gain(2, value);
            break;
        case 12: // SAW_GAIN
            source_mixer_->set_gain(0, value);
            break;
        case 13: // PULSE_GAIN
            source_mixer_->set_gain(1, value);
            break;
        case 14: // NOISE_GAIN
            source_mixer_->set_gain(3, value);
            break;
        default:
            break;
    }
}

void Voice::rebuild_graph() {
    graph_->clear();
    // In Phase 13, we use the SourceMixer to combine oscillators
    // PulseOscillator and SubOscillator are pullable processors.
    // However, they are currently managed manually in do_pull to feed the SourceMixer.
    // The graph nodes from here on are the serial chain AFTER the mixer.
    if (filter_) {
        graph_->add_node(filter_.get());
    }
    graph_->add_node(envelope_.get());
}

void Voice::note_on(double frequency) {
    base_frequency_ = frequency;
    oscillator_->set_frequency(frequency);
    envelope_->gate_on();
    lfo_->reset();
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
    lfo_->reset();
    if (filter_) filter_->reset();
}

void Voice::apply_modulation() {
    // Collect modulation source values
    float env_val = 0.0f;
    static float tmp_env[1];
    envelope_->pull(std::span<float>(tmp_env, 1));
    env_val = tmp_env[0];

    float lfo_val = 0.0f;
    static float tmp_lfo[1];
    lfo_->pull(std::span<float>(tmp_lfo, 1));
    lfo_val = tmp_lfo[0];

    current_source_values_[static_cast<size_t>(ModulationSource::Envelope)] = env_val;
    current_source_values_[static_cast<size_t>(ModulationSource::LFO)] = lfo_val;

    // Apply Pitch Modulation
    float pitch_mod = matrix_.sum_for_target(ModulationTarget::Pitch, current_source_values_);
    oscillator_->set_frequency(base_frequency_ * std::pow(2.0, static_cast<double>(pitch_mod)));

    // Apply Cutoff Modulation
    if (filter_) {
        float cutoff_mod = matrix_.sum_for_target(ModulationTarget::Cutoff, current_source_values_);
        filter_->set_cutoff(base_cutoff_ * std::pow(2.0f, cutoff_mod));
        float res_mod = matrix_.sum_for_target(ModulationTarget::Resonance, current_source_values_);
        filter_->set_resonance(std::clamp(base_resonance_ + res_mod, 0.0f, 0.99f));
    }

    // Apply Amplitude Modulation
    float amp_mod = matrix_.sum_for_target(ModulationTarget::Amplitude, current_source_values_);
    base_amplitude_ = std::clamp(1.0f + amp_mod, 0.0f, 2.0f);

    // Apply PWM
    float pw_mod = matrix_.sum_for_target(ModulationTarget::PulseWidth, current_source_values_);
    if (auto* pulse_osc = dynamic_cast<PulseOscillatorProcessor*>(oscillator_.get())) {
        pulse_osc->set_pulse_width_modulation(pw_mod);
    }
}

void Voice::do_pull(std::span<float> output, const VoiceContext* context) {
    if (!envelope_->is_active()) {
        std::fill(output.begin(), output.end(), 0.0f);
        return;
    }
    apply_modulation();

    // Pull from oscillators
    auto block = graph_->borrow_buffer();
    std::span<float> pulse_span(block->left.data(), output.size());
    std::span<float> sub_span(block->right.data(), output.size());

    oscillator_->pull(pulse_span, context);
    sub_oscillator_->pull(sub_span, context);

    // Mix them into output using SourceMixer (tanh)
    // Headroom Scaling: 0.707 to maintain headroom before tanh
    for (size_t i = 0; i < output.size(); ++i) {
        std::array<float, SourceMixer::NUM_CHANNELS> inputs{};
        inputs[0] = 0.0f; // Saw (not yet implemented as separate node)
        inputs[1] = pulse_span[i] * 0.707f;
        inputs[2] = sub_span[i] * 0.707f;
        inputs[3] = 0.0f; // Noise
        inputs[4] = 0.0f;
        output[i] = source_mixer_->mix(inputs);
    }

    // Pass through the rest of the graph (Filter -> Envelope)
    graph_->pull(output, context);

    float final_gain = base_amplitude_;
    for (auto& sample : output) {
        sample *= final_gain;
    }
}

void Voice::do_pull(AudioBuffer& output, const VoiceContext* context) {
    apply_modulation();
    graph_->pull(output, context);
}

} // namespace audio
