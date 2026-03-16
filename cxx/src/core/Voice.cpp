/**
 * @file Voice.cpp
 * @brief Implementation of the Voice class with Strictly Mono Signal Path.
 */

#include "Voice.hpp"
#include "oscillator/PulseOscillatorProcessor.hpp"
#include "filter/MoogLadderProcessor.hpp"
#include "filter/DiodeLadderProcessor.hpp"
#include "routing/CompositeGenerator.hpp"
#include <vector>
#include <cmath>
#include <stdexcept>

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
    
    // 2. ADSR + VCA: Envelope produces PORT_CONTROL signal; VCA applies it to audio.
    envelope_ = std::make_unique<AdsrEnvelopeProcessor>(sample_rate);
    vca_ = std::make_unique<VcaProcessor>();
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

    AudioLogger::instance().log_message("VOICE", "VCA Modulation Connected");

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
        case 4: { // Global ID 4 -> VCA Attack
            auto* env = baked_ ? dynamic_cast<AdsrEnvelopeProcessor*>(find_by_tag("ENV")) : envelope_.get();
            if (env) env->set_attack_time(value);
            break;
        }
        case 5: { // Global ID 5 -> VCA Decay
            auto* env = baked_ ? dynamic_cast<AdsrEnvelopeProcessor*>(find_by_tag("ENV")) : envelope_.get();
            if (env) env->set_decay_time(value);
            break;
        }
        case 6: { // Global ID 6 -> VCA Sustain
            auto* env = baked_ ? dynamic_cast<AdsrEnvelopeProcessor*>(find_by_tag("ENV")) : envelope_.get();
            if (env) env->set_sustain_level(value);
            break;
        }
        case 7: { // Global ID 7 -> VCA Release
            auto* env = baked_ ? dynamic_cast<AdsrEnvelopeProcessor*>(find_by_tag("ENV")) : envelope_.get();
            if (env) env->set_release_time(value);
            break;
        }
        case 11: { // SUB_GAIN
            auto* vco = baked_ ? dynamic_cast<CompositeGenerator*>(find_by_tag("VCO")) : nullptr;
            if (vco) vco->mixer().set_gain(2, value); else source_mixer_->set_gain(2, value);
            break;
        }
        case 12: { // SAW_GAIN
            auto* vco = baked_ ? dynamic_cast<CompositeGenerator*>(find_by_tag("VCO")) : nullptr;
            if (vco) vco->mixer().set_gain(0, value); else source_mixer_->set_gain(0, value);
            break;
        }
        case 13: { // PULSE_GAIN
            auto* vco = baked_ ? dynamic_cast<CompositeGenerator*>(find_by_tag("VCO")) : nullptr;
            if (vco) vco->mixer().set_gain(1, value); else source_mixer_->set_gain(1, value);
            break;
        }
        case 15: { // SINE_GAIN
            auto* vco = baked_ ? dynamic_cast<CompositeGenerator*>(find_by_tag("VCO")) : nullptr;
            if (vco) vco->mixer().set_gain(3, value); else source_mixer_->set_gain(3, value);
            break;
        }
        case 16: { // TRIANGLE_GAIN
            auto* vco = baked_ ? dynamic_cast<CompositeGenerator*>(find_by_tag("VCO")) : nullptr;
            if (vco) vco->mixer().set_gain(4, value); else source_mixer_->set_gain(4, value);
            break;
        }
        case 17: { // WAVETABLE_GAIN
            auto* vco = baked_ ? dynamic_cast<CompositeGenerator*>(find_by_tag("VCO")) : nullptr;
            if (vco) vco->mixer().set_gain(5, value); else source_mixer_->set_gain(5, value);
            break;
        }
        case 18: { // WAVETABLE_TYPE
            auto* vco = baked_ ? dynamic_cast<CompositeGenerator*>(find_by_tag("VCO")) : nullptr;
            auto wtype = static_cast<WaveType>(static_cast<int>(value));
            if (vco) vco->wavetable_osc().setWaveType(wtype);
            else wavetable_oscillator_->setWaveType(wtype);
            break;
        }
        case 14: { // PULSE_WIDTH (Native)
            if (baked_) {
                auto* vco = dynamic_cast<CompositeGenerator*>(find_by_tag("VCO"));
                if (vco) vco->pulse_osc().set_pulse_width(value);
            } else if (auto* pulse_osc = dynamic_cast<PulseOscillatorProcessor*>(oscillator_.get())) {
                pulse_osc->set_pulse_width(value);
            }
            break;
        }
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

    if (baked_) {
        for (auto& entry : signal_chain_) entry.node->reset();
        lfo_->reset();
        if (filter_) filter_->reset();
        if (auto* vco = dynamic_cast<CompositeGenerator*>(find_by_tag("VCO"))) {
            vco->set_frequency(frequency);
        }
        if (auto* env = dynamic_cast<AdsrEnvelopeProcessor*>(find_by_tag("ENV"))) {
            env->gate_on();
        }
        return;
    }

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
    if (baked_) {
        if (auto* env = dynamic_cast<AdsrEnvelopeProcessor*>(find_by_tag("ENV"))) {
            env->gate_off();
        }
        active_ = false;
        return;
    }
    envelope_->gate_off();
    active_ = false;
}

bool Voice::is_active() const {
    if (baked_) {
        if (active_) return true;
        for (const auto& entry : signal_chain_) {
            if (auto* env = dynamic_cast<AdsrEnvelopeProcessor*>(entry.node.get())) {
                return env->is_active();
            }
        }
        return false;
    }
    return active_ || envelope_->is_active();
}

bool Voice::is_releasing() const {
    if (baked_) {
        for (const auto& entry : signal_chain_) {
            if (auto* env = dynamic_cast<AdsrEnvelopeProcessor*>(entry.node.get())) {
                return !active_ && env->is_active();
            }
        }
        return false;
    }
    // A voice is releasing if the gate is off but the envelope is still active.
    return !active_ && envelope_->is_active();
}

void Voice::reset() {
    if (baked_) {
        for (auto& entry : signal_chain_) entry.node->reset();
        lfo_->reset();
        if (filter_) filter_->reset();
        return;
    }
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
    if (baked_) {
        auto* env_node = dynamic_cast<AdsrEnvelopeProcessor*>(find_by_tag("ENV"));
        float env_val = env_node ? env_node->get_level() : 0.0f;

        float l_buf[1];
        std::span<float> l_span(l_buf, 1);
        lfo_->pull(l_span);
        float lfo_val = l_buf[0];

        current_source_values_[static_cast<size_t>(ModulationSource::Envelope)] = env_val;
        current_source_values_[static_cast<size_t>(ModulationSource::LFO)] = lfo_val;

        float pitch_mod = matrix_.sum_for_target(ModulationTarget::Pitch, current_source_values_);
        current_frequency_ = base_frequency_ * std::pow(2.0, static_cast<double>(pitch_mod));
        if (current_frequency_ < 20.0) current_frequency_ = base_frequency_;

        if (auto* vco = dynamic_cast<CompositeGenerator*>(find_by_tag("VCO"))) {
            vco->set_frequency(current_frequency_);
        }

        if (filter_) {
            float cutoff_mod = matrix_.sum_for_target(ModulationTarget::Cutoff, current_source_values_);
            float mod_cutoff = base_cutoff_ * std::pow(2.0f, cutoff_mod);
            if (mod_cutoff < 20.0f) mod_cutoff = 20.0f;
            filter_->set_cutoff(mod_cutoff);
            float res_mod = matrix_.sum_for_target(ModulationTarget::Resonance, current_source_values_);
            filter_->set_resonance(std::clamp(base_resonance_ + res_mod, 0.0f, 0.99f));
        }
        return;
    }

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
    // IMPORTANT: The main ADSR gate is handled by envelope_->pull() at the end.
    // Modulation here is for ADDITIONAL scaling (e.g. LFO -> Amp).
    if (matrix_.has_no_connections(ModulationTarget::Amplitude)) {
        current_amplitude_ = base_amplitude_;
    } else {
        float amp_mod_sum = matrix_.sum_for_target(ModulationTarget::Amplitude, current_source_values_);
        
        // ARCHITECTURAL CHOICE: If the sum is 0 (e.g. no active modulators),
        // we default to unity scaling (1.0) so the VCA remains open.
        if (std::abs(amp_mod_sum) < 0.001f) {
            current_amplitude_ = base_amplitude_;
        } else {
            current_amplitude_ = base_amplitude_ * std::clamp(amp_mod_sum, 0.0f, 2.0f);
        }
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
    // -------------------------------------------------------------------------
    // Phase 14 baked path: iterate signal_chain_ with tag-dispatch for ENV/VCA.
    // -------------------------------------------------------------------------
    if (baked_) {
        apply_modulation();

        // Borrow a buffer for the envelope control signal (PORT_CONTROL).
        // RT-SAFE: borrow_buffer() is a lock-free pool allocation.
        auto env_block = graph_->borrow_buffer();
        std::span<float> env_span(env_block->left.data(), output.size());
        bool env_filled = false;

        bool audio_generated = false;
        for (auto& entry : signal_chain_) {
            if (entry.tag == "ENV") {
                // Fill env_span with [0,1] control levels — not audio.
                entry.node->pull(env_span, context);
                env_filled = true;
            } else if (entry.tag == "VCA") {
                // Apply filter (if present) before VCA.
                if (audio_generated && filter_) filter_->pull(output, context);
                // Apply envelope × base_amplitude to the audio buffer in-place.
                if (env_filled) {
                    VcaProcessor::apply(output, env_span, base_amplitude_);
                }
            } else {
                // All other nodes (VCO, …) pull audio in-place.
                entry.node->pull(output, context);
                audio_generated = true;
            }
        }
        return;
    }

    // -------------------------------------------------------------------------
    // Legacy path (unbaked): original hardcoded render loop — unchanged.
    // -------------------------------------------------------------------------

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
    }

    // 3. FILTER: process in-place on the oscillator mix
    if (filter_) {
        filter_->pull(output, context);
    }

    // 4. VCA: envelope produces a PORT_CONTROL buffer; VcaProcessor multiplies audio by it.
    // This is the architecturally correct VCA/Envelope separation (MODULE_DESC §3).
    // RT-SAFE: borrow_buffer() is a pool allocation.
    auto env_block = graph_->borrow_buffer();
    std::span<float> env_span(env_block->left.data(), output.size());
    envelope_->pull(env_span, context);                    // fills env_span with [0,1] levels
    VcaProcessor::apply(output, env_span, base_amplitude_); // audio *= envelope * base_amplitude

    // Audit Trace: Confirm signal is produced AFTER all processing
    float peak = 0.0f;
    for (float s : output) {
        float abs_s = std::abs(s);
        if (abs_s > peak) peak = abs_s;
    }
    
}

// =============================================================================
// Phase 14: Dynamic Signal Chain API
// =============================================================================

void Voice::add_processor(std::unique_ptr<Processor> p, std::string tag) {
    p->set_tag(tag);
    signal_chain_.push_back({std::move(p), std::move(tag)});
    baked_ = false;
}

void Voice::bake() {
    if (signal_chain_.empty()) {
        throw std::logic_error("Voice::bake() called on empty signal_chain_");
    }
    // Generator-First Rule: first node must be a CompositeGenerator.
    if (dynamic_cast<CompositeGenerator*>(signal_chain_[0].node.get()) == nullptr) {
        throw std::logic_error(
            "Voice::bake() failed: signal_chain_[0] is not a CompositeGenerator");
    }
    baked_ = true;
}

Processor* Voice::find_by_tag(std::string_view tag) {
    for (auto& entry : signal_chain_) {
        if (entry.tag == tag) return entry.node.get();
    }
    return nullptr;
}

} // namespace audio
