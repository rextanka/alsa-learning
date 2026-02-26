/**
 * @file Voice.cpp
 * @brief Implementation of the Voice class with Modular Modulation Matrix.
 */

#include "Voice.hpp"
#include "oscillator/PulseOscillatorProcessor.hpp"
#include "oscillator/SubOscillator.hpp"
#include "routing/SourceMixer.hpp"
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
    // In our new architecture, we pull oscillator and sub-oscillator manually
    // but we add them to the graph for metadata/lifecycle management.
    graph_->add_node(oscillator_.get());
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
    // 1. Collect modulation source values (block-rate)
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

    // 5. Pulse Width Modulation (Linear)
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

    auto* pulse_osc = dynamic_cast<PulseOscillatorProcessor*>(oscillator_.get());
    size_t frames = std::min(output.size(), MAX_BLOCK_SIZE);

    for (size_t i = 0; i < frames; ++i) {
        float main_out = 0.0f;
        float sub_out = 0.0f;

        // Advance oscillators sample by sample
        float tmp[1];
        std::span<float> single_frame(tmp, 1);
        oscillator_->pull(single_frame, context);
        main_out = tmp[0];

        if (pulse_osc) {
            sub_out = static_cast<float>(sub_oscillator_->generate_sample(pulse_osc->get_phase()));
        }

        std::array<float, SourceMixer::NUM_CHANNELS> inputs = {0.0f};
        inputs[1] = main_out; // Channel 1: Pulse/Square
        inputs[2] = sub_out;  // Channel 2: Sub

        output[i] = source_mixer_->mix(inputs);
    }

    // Apply VCA
    float env_gain = current_source_values_[static_cast<size_t>(ModulationSource::Envelope)];
    float final_gain = env_gain * base_amplitude_;
    for (size_t i = 0; i < frames; ++i) {
        output[i] *= final_gain;
    }

    // Filter
    if (filter_) {
        filter_->pull(output, context);
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

int Voice::set_internal_param(const std::string& name, float value) {
    if (name == "attack") { envelope_->set_attack_time(value); return 0; }
    if (name == "decay") { envelope_->set_decay_time(value); return 0; }
    if (name == "sustain") { envelope_->set_sustain_level(value); return 0; }
    if (name == "release") { envelope_->set_release_time(value); return 0; }
    
    if (name == "vcf_cutoff") { base_cutoff_ = value; return 0; }
    if (name == "vcf_res") { base_resonance_ = value; return 0; }
    
    if (name == "pulse_gain") { source_mixer_->set_gain(1, value); return 0; }
    if (name == "sub_gain") { source_mixer_->set_gain(2, value); return 0; }
    
    if (auto* pulse_osc = dynamic_cast<PulseOscillatorProcessor*>(oscillator_.get())) {
        if (name == "osc_pw") { pulse_osc->set_pulse_width(value); return 0; }
    }

    return -1;
}

} // namespace audio
