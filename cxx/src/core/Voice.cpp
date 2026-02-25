/**
 * @file Voice.cpp
 * @brief Implementation of the Voice class with Modular Modulation Matrix.
 */

#include "Voice.hpp"
#include "oscillator/SquareOscillatorProcessor.hpp"
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
    // 1. Oscillator: 50% Pulse (Square Wave)
    oscillator_ = std::make_unique<SquareOscillatorProcessor>(static_cast<double>(sample_rate));
    
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
    // 4000Hz base + 1.0 octave = 8000Hz (at full envelope)
    // The previous hardcoded was 4000 + (gain * 2000), which is linear.
    // Exponential equivalent: 4000 * 2^(0.58) ≈ 6000. 
    // Let's use 0.5 octaves for a musical chiff.
    matrix_.set_connection(ModulationSource::Envelope, ModulationTarget::Cutoff, 0.585f); // log2(6000/4000) approx
    
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
        default:
            break;
    }
}

void Voice::rebuild_graph() {
    graph_->clear();
    graph_->add_node(oscillator_.get());
    if (filter_) {
        graph_->add_node(filter_.get());
    }
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
    // 1. Collect modulation source values (block-rate)
    // For now we use the first sample of the block or a separate pull
    // Envelopes and LFOs are pulled here to get their current value for the block.
    
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
    // Velocity/Aftertouch can be added via VoiceContext

    // 2. Apply Pitch Modulation (Exponential)
    float pitch_mod = matrix_.sum_for_target(ModulationTarget::Pitch, current_source_values_);
    double final_freq = base_frequency_ * std::pow(2.0, static_cast<double>(pitch_mod));
    oscillator_->set_frequency(final_freq);

    // 3. Apply Cutoff Modulation (Exponential)
    if (filter_) {
        float cutoff_mod = matrix_.sum_for_target(ModulationTarget::Cutoff, current_source_values_);
        float final_cutoff = base_cutoff_ * std::pow(2.0f, cutoff_mod);
        filter_->set_cutoff(std::clamp(final_cutoff, 20.0f, 20000.0f));

        // Resonance Modulation (Linear)
        float res_mod = matrix_.sum_for_target(ModulationTarget::Resonance, current_source_values_);
        filter_->set_resonance(std::clamp(base_resonance_ + res_mod, 0.0f, 0.99f));
    }

    // 4. Amplitude Modulation (Linear/Factor)
    float amp_mod = matrix_.sum_for_target(ModulationTarget::Amplitude, current_source_values_);
    base_amplitude_ = std::clamp(1.0f + amp_mod, 0.0f, 2.0f);
}

void Voice::do_pull(std::span<float> output, const VoiceContext* context) {
    // Apply modular modulation for this block
    apply_modulation();

    // Process the graph (Oscillator -> Filter)
    graph_->pull(output, context);

    // Apply VCA (Final gain = Base Amplitude * Env)
    // Note: envelope was already pulled in apply_modulation to get block value,
    // but for sample-accurate VCA we pull it again or use the cached block value.
    // To keep it simple and consistent with previous "chiff" implementation:
    float env_gain = current_source_values_[static_cast<size_t>(ModulationSource::Envelope)];
    float final_gain = env_gain * base_amplitude_;

    for (auto& sample : output) {
        sample *= final_gain;
    }
}

void Voice::do_pull(AudioBuffer& output, const VoiceContext* context) {
    apply_modulation();

    graph_->pull(output, context);

    float env_gain = current_source_values_[static_cast<size_t>(ModulationSource::Envelope)];
    float final_gain = env_gain * base_amplitude_;

    // Constant power panning
    float pan_rad = (pan_ + 1.0f) * (M_PI / 4.0f);
    float gain_l = std::cos(pan_rad) * final_gain;
    float gain_r = std::sin(pan_rad) * final_gain;

    for (size_t i = 0; i < output.frames(); ++i) {
        output.left[i] *= gain_l;
        output.right[i] *= gain_r;
    }
}

} // namespace audio
