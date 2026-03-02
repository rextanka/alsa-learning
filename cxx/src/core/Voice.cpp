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
    saw_oscillator_ = std::make_unique<SawtoothOscillatorProcessor>(sample_rate);
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

    // Clear all default modulations to ensure a clean state for "Beep" Surgery
    matrix_.clear_all();
    
    // DEFAULT VCA CONNECTION: Envelope -> Amplitude (Intensity: 1.0f)
    // This ensures all legacy tests and default voices have audible output.
    matrix_.set_connection(ModulationSource::Envelope, ModulationTarget::Amplitude, 1.0f);

    current_source_values_.fill(0.0f);
    log_counter_ = 0;
    AudioLogger::instance().log_event("SR_CHECK", static_cast<float>(sample_rate_));
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
            if (filter_) filter_->set_cutoff(base_cutoff_);
            break;
        case 2: // RESONANCE
            base_resonance_ = std::clamp(value, 0.0f, 1.0f);
            if (filter_) filter_->set_resonance(base_resonance_);
            break;
        case 4: // AMP_DECAY
            envelope_->set_decay_time(value);
            break;
        case 5: // AMP_SUSTAIN
            envelope_->set_sustain_level(value);
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
        case 14: // PULSE_WIDTH (Re-mapped for test)
            if (auto* pulse_osc = dynamic_cast<PulseOscillatorProcessor*>(oscillator_.get())) {
                pulse_osc->set_pulse_width(value);
            }
            break;
        case 17: // VCF_ENV_AMOUNT (Bypass logic)
            // Modulation Matrix would handle this, but we've cleared it.
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
    // Ensure all processors are reset to clear stuck DC or phase
    oscillator_->reset();
    sub_oscillator_->reset();
    saw_oscillator_->reset();
    envelope_->reset();
    lfo_->reset();
    if (filter_) filter_->reset();

    // Set frequency after reset to ensure current_freq is correct
    oscillator_->set_frequency(frequency);
    saw_oscillator_->set_frequency(frequency);
    
    // Hardwire VCA if missing
    if (matrix_.sum_for_target(ModulationTarget::Amplitude, current_source_values_) <= 0.001f) {
         matrix_.set_connection(ModulationSource::Envelope, ModulationTarget::Amplitude, 1.0f);
    }
    
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
    sub_oscillator_->reset();
    saw_oscillator_->reset();
    envelope_->reset();
    lfo_->reset();
    if (filter_) filter_->reset();
}

void Voice::apply_modulation() {
    // Collect modulation source values
    float env_val = 0.0f;
    float tmp_env_buf[1];
    envelope_->pull(std::span<float>(tmp_env_buf, 1));
    env_val = tmp_env_buf[0];

    float lfo_val = 0.0f;
    float tmp_lfo_buf[1];
    lfo_->pull(std::span<float>(tmp_lfo_buf, 1));
    lfo_val = tmp_lfo_buf[0];

    current_source_values_[static_cast<size_t>(ModulationSource::Envelope)] = env_val;
    current_source_values_[static_cast<size_t>(ModulationSource::LFO)] = lfo_val;

    // Apply Pitch Modulation
    float pitch_mod = matrix_.sum_for_target(ModulationTarget::Pitch, current_source_values_);
    double mod_freq = base_frequency_ * std::pow(2.0, static_cast<double>(pitch_mod));
    
    // Safety check for frequency floor
    if (mod_freq < 20.0) mod_freq = base_frequency_;

    oscillator_->set_frequency(mod_freq);

    // Apply Cutoff Modulation
    if (filter_) {
        float cutoff_mod = matrix_.sum_for_target(ModulationTarget::Cutoff, current_source_values_);
        float mod_cutoff = base_cutoff_ * std::pow(2.0f, cutoff_mod);
        if (mod_cutoff < 20.0f) mod_cutoff = 20.0f;
        filter_->set_cutoff(mod_cutoff);
        
        float res_mod = matrix_.sum_for_target(ModulationTarget::Resonance, current_source_values_);
        filter_->set_resonance(std::clamp(base_resonance_ + res_mod, 0.0f, 0.99f));
    }

    // Apply Amplitude Modulation
    float amp_mod = matrix_.sum_for_target(ModulationTarget::Amplitude, current_source_values_);
    base_amplitude_ = std::clamp(amp_mod, 0.0f, 1.0f); // Amplitude modulation is primary gain

    // Apply PWM
    float pw_mod = matrix_.sum_for_target(ModulationTarget::PulseWidth, current_source_values_);
    if (auto* pulse_osc = dynamic_cast<PulseOscillatorProcessor*>(oscillator_.get())) {
        pulse_osc->set_pulse_width_modulation(pw_mod);
    }
}

void Voice::do_pull(std::span<float> output, const VoiceContext* context) {
    // 1. BLOCK-LEVEL PULL FOR INDEPENDENT SOURCES
    auto block = graph_->borrow_buffer();
    std::span<float> saw_span(block->left.data(), output.size());

    // Ensure frequency is set before pulling
    saw_oscillator_->set_frequency(base_frequency_);
    
    // Pull independent oscillators as blocks
    saw_oscillator_->pull(saw_span, context);

    // 2. INTERLEAVED SAMPLE LOOP FOR PHASE-LOCKED SOURCES
    auto* pulse_osc = dynamic_cast<PulseOscillatorProcessor*>(oscillator_.get());
    auto* sub_osc = dynamic_cast<SubOscillator*>(sub_oscillator_.get());

    pulse_osc->set_frequency(base_frequency_);

    float saw_gain = source_mixer_->get_gain(0);
    float pulse_gain = source_mixer_->get_gain(1);
    float sub_gain = source_mixer_->get_gain(2);

    // Pull envelope value for smooth gating
    float env_buf[1];
    envelope_->pull(std::span<float>(env_buf, 1), context);
    float current_env = env_buf[0];

    for (size_t i = 0; i < output.size(); ++i) {
        // Pulse increments phase and returns sample
        float p_sample = static_cast<float>(pulse_osc->tick());
        
        // Sub-osc locks to pulse phase
        float s_sample = static_cast<float>(sub_osc->generate_sample(pulse_osc->get_phase()));
        
        // Summing the active sources (Saw, Pulse, Sub)
        output[i] = (saw_span[i] * saw_gain + p_sample * pulse_gain + s_sample * sub_gain) * current_env;
    }

    // Diagnostic Trace
    if (log_counter_++ % 128 == 0) {
        AudioLogger::instance().log_event("REPAIR_ENV", current_env);
    }
}

void Voice::do_pull(AudioBuffer& output, const VoiceContext* context) {
    apply_modulation();
    graph_->pull(output, context);
}

} // namespace audio
