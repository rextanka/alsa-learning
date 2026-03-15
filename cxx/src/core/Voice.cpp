/**
 * @file Voice.cpp
 * @brief Implementation of the Voice class with Strictly Mono Signal Path.
 */

#include "Voice.hpp"
#include "oscillator/PulseOscillatorProcessor.hpp"
#include "filter/MoogLadderProcessor.hpp"
#include "filter/DiodeLadderProcessor.hpp"
#include <vector>
#include <cmath>
#include <iostream>

namespace audio {

Voice::Voice(int sample_rate)
    : base_frequency_(440.0)
    , base_cutoff_(4000.0f)
    , base_resonance_(0.4f)
    , base_amplitude_(1.0f)
    , current_frequency_(440.0)
    , current_amplitude_(1.0f)
    , sample_rate_(sample_rate)
    , pan_(0.0f)
    , active_(false)
{
    // 1. Oscillator: Pulse Oscillator (SH-101/Juno Style)
    oscillator_ = std::make_unique<PulseOscillatorProcessor>(sample_rate);
    sub_oscillator_ = std::make_unique<SubOscillator>();
    saw_oscillator_ = std::make_unique<SawtoothOscillatorProcessor>(sample_rate);
    sine_oscillator_ = std::make_unique<SineOscillatorProcessor>(sample_rate);
    triangle_oscillator_ = std::make_unique<TriangleOscillatorProcessor>(sample_rate);
    wavetable_oscillator_ = std::make_unique<WavetableOscillatorProcessor>(static_cast<double>(sample_rate));
    source_mixer_ = std::make_unique<SourceMixer>();
    source_mixer_->set_gain(1, 1.0f); // Default main pulse gain
    source_mixer_->set_gain(2, 0.5f); // Default sub-osc gain
    
    // 2. ADSR: Default settings
    envelope_ = std::make_unique<AdsrEnvelopeProcessor>(sample_rate);
    envelope_->set_attack_time(0.05f);
    envelope_->set_decay_time(0.1f);
    envelope_->set_sustain_level(0.7f);
    envelope_->set_release_time(0.1f);

    // 3. Filter: Starts as nullptr (Flexible Topology)
    filter_ = nullptr;
    base_cutoff_ = 20000.0f; // Ensure wide open by default

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

    // BEEP_SURGERY: Initialize base amplitude to 1.0. 
    // This will be scaled by the modulation envelope in do_pull.
    base_amplitude_ = 1.0f;

    std::cout << "[VOICE] VCA Modulation Connected" << std::endl;

    current_source_values_.fill(0.0f);
    log_counter_ = 0;
    AudioLogger::instance().log_event("SR_CHECK", static_cast<float>(sample_rate_));
}

void Voice::set_filter_type(std::unique_ptr<FilterProcessor> filter) {
    filter_ = std::move(filter);
    rebuild_graph();
}

void Voice::set_filter_type(int type) {
    std::unique_ptr<FilterProcessor> new_filter;
    if (type == 0) {
        new_filter = std::make_unique<MoogLadderProcessor>(sample_rate_);
    } else if (type == 1) {
        new_filter = std::make_unique<DiodeLadderProcessor>(sample_rate_);
    }
    
    if (new_filter) {
        // PRE-INIT: Set cutoff/res BEFORE attaching to chain to avoid silence
        new_filter->set_cutoff(base_cutoff_);
        new_filter->set_resonance(base_resonance_);
        filter_ = std::move(new_filter);
    } else {
        filter_ = nullptr;
    }
    
    rebuild_graph();
}

void Voice::set_pan(float pan) {
    pan_ = std::clamp(pan, -1.0f, 1.0f);
}

void Voice::set_parameter(int param, float value) {
    // Map string-based parameters (sent via AudioBridge) to internal components
    switch (param) {
        case 0: // PITCH
            base_frequency_ = static_cast<double>(value);
            break;
        case 8: // AMP_BASE (Internal routing for beep tests)
            base_amplitude_ = std::clamp(value, 0.0f, 1.0f);
            break;
        case 1: // CUTOFF
            base_cutoff_ = std::max(20.0f, value);
            if (filter_) filter_->set_cutoff(base_cutoff_);
            break;
        case 2: // RESONANCE
            base_resonance_ = std::clamp(value, 0.0f, 1.0f);
            if (filter_) filter_->set_resonance(base_resonance_);
            break;
        case 3: // VCF_ENV_AMOUNT (Tag VCF, Internal ID 3)
            break;
        case 4: // Global ID 4 -> VCA Attack
            envelope_->set_attack_time(value);
            break;
        case 5: // Global ID 5 -> VCA Decay
            envelope_->set_decay_time(value);
            break;
        case 6: // Global ID 6 -> VCA Sustain
            envelope_->set_sustain_level(value);
            break;
        case 7: // Global ID 7 -> VCA Release
            envelope_->set_release_time(value);
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
        case 15: // SINE_GAIN (New mapping for Tuner Tool)
            source_mixer_->set_gain(3, value);
            break;
        case 16: // TRIANGLE_GAIN (New mapping for Tuner Tool)
            source_mixer_->set_gain(4, value);
            break;
        case 17: // WAVETABLE_GAIN
            source_mixer_->set_gain(5, value);
            break;
        case 18: // WAVETABLE_TYPE
            wavetable_oscillator_->setWaveType(static_cast<WaveType>(static_cast<int>(value)));
            break;
        case 14: // PULSE_WIDTH (Native)
            if (auto* pulse_osc = dynamic_cast<PulseOscillatorProcessor*>(oscillator_.get())) {
                pulse_osc->set_pulse_width(value);
            }
            break;
        case 10: // PULSE_WIDTH (Legacy Alias)
            set_parameter(14, value);
            break;
        default:
            break;
    }
}

void Voice::rebuild_graph() {
    graph_->clear();
    // Linear Mono Chain after the mixer
    if (filter_) {
        graph_->add_node(filter_.get());
    }
    // DO NOT add envelope to graph pulled by do_pull, 
    // as Voice handles it manually to ensure VCA behavior.
    graph_->add_node(source_mixer_.get()); // Ensure mixer is in graph for param sync
    graph_->add_node(oscillator_.get());
    graph_->add_node(sub_oscillator_.get());
    graph_->add_node(saw_oscillator_.get());
    graph_->add_node(sine_oscillator_.get());
    graph_->add_node(triangle_oscillator_.get());
    graph_->add_node(wavetable_oscillator_.get());
}

void Voice::note_on(double frequency) {
    active_ = true;
    base_frequency_ = frequency;
    // Ensure all processors are reset to clear stuck DC or phase
    oscillator_->reset();
    sub_oscillator_->reset();
    saw_oscillator_->reset();
    sine_oscillator_->reset();
    triangle_oscillator_->reset();
    wavetable_oscillator_->reset();
    envelope_->reset();
    lfo_->reset();
    if (filter_) filter_->reset();

    // Set frequency after reset to ensure current_freq is correct
    oscillator_->set_frequency(frequency);
    saw_oscillator_->set_frequency(frequency);
    sine_oscillator_->set_frequency(frequency);
    triangle_oscillator_->set_frequency(frequency);
    wavetable_oscillator_->setFrequency(frequency);
    
    // Hardwire VCA if missing
    if (matrix_.sum_for_target(ModulationTarget::Amplitude, current_source_values_) <= 0.001f) {
         matrix_.set_connection(ModulationSource::Envelope, ModulationTarget::Amplitude, 1.0f);
    }
    
    envelope_->gate_on();
}

void Voice::note_off() {
    envelope_->gate_off();
    active_ = false;
}

bool Voice::is_active() const { 
    return active_ || envelope_->is_active();
}

bool Voice::is_releasing() const {
    // A voice is releasing if the gate is off but the envelope is still active.
    return !active_ && envelope_->is_active();
}

void Voice::reset() {
    oscillator_->reset();
    sub_oscillator_->reset();
    saw_oscillator_->reset();
    sine_oscillator_->reset();
    triangle_oscillator_->reset();
    wavetable_oscillator_->reset();
    envelope_->reset();
    lfo_->reset();
    if (filter_) filter_->reset();
}

void Voice::apply_modulation() {
    // Collect modulation source values
    // RT-Safe: Get current level from processors without non-destructive pull
    float env_val = envelope_->get_level();
    
    // Manual pull for LFO since it's a Processor but we're block-rate mixing
    float l_buf[1];
    std::span<float> l_span(l_buf, 1);
    lfo_->pull(l_span);
    float lfo_val = l_buf[0];

    current_source_values_[static_cast<size_t>(ModulationSource::Envelope)] = env_val;
    current_source_values_[static_cast<size_t>(ModulationSource::LFO)] = lfo_val;

    // 1. Apply Pitch Modulation
    float pitch_mod = matrix_.sum_for_target(ModulationTarget::Pitch, current_source_values_);
    current_frequency_ = base_frequency_ * std::pow(2.0, static_cast<double>(pitch_mod));
    
    // Safety check for frequency floor
    if (current_frequency_ < 20.0) current_frequency_ = base_frequency_;

    oscillator_->set_frequency(current_frequency_);
    saw_oscillator_->set_frequency(current_frequency_);
    sine_oscillator_->set_frequency(current_frequency_);
    triangle_oscillator_->set_frequency(current_frequency_);
    wavetable_oscillator_->setFrequency(current_frequency_);

    // 2. Apply Cutoff Modulation
    if (filter_) {
        float cutoff_mod = matrix_.sum_for_target(ModulationTarget::Cutoff, current_source_values_);
        float mod_cutoff = base_cutoff_ * std::pow(2.0f, cutoff_mod);
        if (mod_cutoff < 20.0f) mod_cutoff = 20.0f;
        filter_->set_cutoff(mod_cutoff);
        
        float res_mod = matrix_.sum_for_target(ModulationTarget::Resonance, current_source_values_);
        filter_->set_resonance(std::clamp(base_resonance_ + res_mod, 0.0f, 0.99f));
    }

    // 3. Apply Amplitude Modulation (Safe Fallback)
    if (matrix_.has_no_connections(ModulationTarget::Amplitude)) {
        current_amplitude_ = base_amplitude_;
    } else {
        float amp_mod = matrix_.sum_for_target(ModulationTarget::Amplitude, current_source_values_);
        current_amplitude_ = std::clamp(base_amplitude_ * amp_mod, 0.0f, 1.0f);
    }

    // 4. Apply PWM
    float pw_mod = matrix_.sum_for_target(ModulationTarget::PulseWidth, current_source_values_);
    if (auto* pulse_osc = dynamic_cast<PulseOscillatorProcessor*>(oscillator_.get())) {
        pulse_osc->set_pulse_width(pw_mod);
    }
}

void Voice::do_pull(std::span<float> output, const VoiceContext* context) {
    pull_mono(output, context);
}

void Voice::pull_mono(std::span<float> output, const VoiceContext* context) {
    // 1. UPDATE MODULATION
    apply_modulation();

    // 2. RENDER OSCILLATORS (Interleaved mix)
    // Borrow a buffer from the pool for independent sources
    auto block = graph_->borrow_buffer();
    std::span<float> saw_span(block->left.data(), output.size());
    
    // Ensure independent oscillators are set correctly
    saw_oscillator_->set_frequency(current_frequency_);
    saw_oscillator_->pull(saw_span, context);

    auto* pulse_osc = dynamic_cast<PulseOscillatorProcessor*>(oscillator_.get());
    auto* sub_osc = dynamic_cast<SubOscillator*>(sub_oscillator_.get());

    pulse_osc->set_frequency(current_frequency_);

    // Render interleaved oscillator mix into output span
    for (size_t i = 0; i < output.size(); ++i) {
        float p_sample = static_cast<float>(pulse_osc->tick());
        float s_sample = static_cast<float>(sub_osc->generate_sample(pulse_osc->get_phase()));
        float sine_sample = static_cast<float>(sine_oscillator_->tick());
        float tri_sample = static_cast<float>(triangle_oscillator_->tick());
        
        // Manual pull for wavetable since it's a Processor but we're tick-mixing
        // Note: Wavetable generates in-place, so we just use the first sample of a tmp buffer
        float w_buf[1];
        std::span<float> w_span(w_buf, 1);
        wavetable_oscillator_->pull(w_span, context);
        float w_sample = w_buf[0];

        // Mix using SourceMixer logic
        std::array<float, SourceMixer::NUM_CHANNELS> inputs;
        inputs.fill(0.0f);
        inputs[0] = saw_span[i];
        inputs[1] = p_sample;
        inputs[2] = s_sample;
        inputs[3] = sine_sample;
        inputs[4] = tri_sample;
        inputs[5] = w_sample;
        
        output[i] = source_mixer_->mix(inputs);
        
        // Final amplitude scaling (RT-Safe)
        output[i] *= current_amplitude_;
    }

    // 3. PROCESS THROUGH MODIFIERS (Manual Serial Processing)
    // Filter and Envelope process in-place on the oscillator mix
    if (filter_) {
        filter_->pull(output, context);
    }
    envelope_->pull(output, context);

    // Audit Trace: Confirm signal is produced AFTER all processing
    float peak = 0.0f;
    for (float s : output) {
        float abs_s = std::abs(s);
        if (abs_s > peak) peak = abs_s;
    }
}

} // namespace audio
