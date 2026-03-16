/**
 * @file Voice.cpp
 * @brief Implementation of the Voice class with Strictly Mono Signal Path.
 */

#include "Voice.hpp"
#include "filter/MoogLadderProcessor.hpp"
#include "filter/DiodeLadderProcessor.hpp"
#include <cmath>
#include <stdexcept>

namespace audio {

Voice::Voice(int sample_rate)
    : base_frequency_(440.0)
    , base_cutoff_(20000.0f)
    , base_resonance_(0.4f)
    , base_amplitude_(1.0f)
    , current_frequency_(440.0)
    , current_amplitude_(1.0f)
    , sample_rate_(sample_rate)
    , pan_(0.0f)
    , active_(false)
{
    lfo_ = std::make_unique<LfoProcessor>(sample_rate);
    lfo_->set_frequency(5.0);
    lfo_->set_intensity(0.0f);

    graph_ = std::make_unique<AudioGraph>();

    matrix_.clear_all();
    matrix_.set_connection(ModulationSource::Envelope, ModulationTarget::Amplitude, 1.0f);

    current_source_values_.fill(0.0f);
    log_counter_ = 0;
    AudioLogger::instance().log_event("SR_CHECK", static_cast<float>(sample_rate_));
}

void Voice::set_filter_type(std::unique_ptr<FilterProcessor> filter) {
    filter_ = std::move(filter);
}

void Voice::set_filter_type(int type) {
    std::unique_ptr<FilterProcessor> new_filter;
    if (type == 0) {
        new_filter = std::make_unique<MoogLadderProcessor>(sample_rate_);
    } else if (type == 1) {
        new_filter = std::make_unique<DiodeLadderProcessor>(sample_rate_);
    }

    if (new_filter) {
        new_filter->set_cutoff(base_cutoff_);
        new_filter->set_resonance(base_resonance_);
        filter_ = std::move(new_filter);
    } else {
        filter_ = nullptr;
    }
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
        case 4: { // VCA Attack
            if (auto* env = dynamic_cast<AdsrEnvelopeProcessor*>(find_by_tag("ENV"))) env->set_attack_time(value);
            break;
        }
        case 5: { // VCA Decay
            if (auto* env = dynamic_cast<AdsrEnvelopeProcessor*>(find_by_tag("ENV"))) env->set_decay_time(value);
            break;
        }
        case 6: { // VCA Sustain
            if (auto* env = dynamic_cast<AdsrEnvelopeProcessor*>(find_by_tag("ENV"))) env->set_sustain_level(value);
            break;
        }
        case 7: { // VCA Release
            if (auto* env = dynamic_cast<AdsrEnvelopeProcessor*>(find_by_tag("ENV"))) env->set_release_time(value);
            break;
        }
        case 11: { // SUB_GAIN
            if (auto* vco = dynamic_cast<CompositeGenerator*>(find_by_tag("VCO"))) vco->mixer().set_gain(2, value);
            break;
        }
        case 12: { // SAW_GAIN
            if (auto* vco = dynamic_cast<CompositeGenerator*>(find_by_tag("VCO"))) vco->mixer().set_gain(0, value);
            break;
        }
        case 13: { // PULSE_GAIN
            if (auto* vco = dynamic_cast<CompositeGenerator*>(find_by_tag("VCO"))) vco->mixer().set_gain(1, value);
            break;
        }
        case 15: { // SINE_GAIN
            if (auto* vco = dynamic_cast<CompositeGenerator*>(find_by_tag("VCO"))) vco->mixer().set_gain(3, value);
            break;
        }
        case 16: { // TRIANGLE_GAIN
            if (auto* vco = dynamic_cast<CompositeGenerator*>(find_by_tag("VCO"))) vco->mixer().set_gain(4, value);
            break;
        }
        case 17: { // WAVETABLE_GAIN
            if (auto* vco = dynamic_cast<CompositeGenerator*>(find_by_tag("VCO"))) vco->mixer().set_gain(5, value);
            break;
        }
        case 18: { // NOISE_GAIN
            if (auto* vco = dynamic_cast<CompositeGenerator*>(find_by_tag("VCO"))) vco->mixer().set_gain(6, value);
            break;
        }
        case 19: { // WAVETABLE_TYPE
            auto wtype = static_cast<WaveType>(static_cast<int>(value));
            if (auto* vco = dynamic_cast<CompositeGenerator*>(find_by_tag("VCO"))) vco->wavetable_osc().setWaveType(wtype);
            break;
        }
        case 10: // PULSE_WIDTH (alias)
            [[fallthrough]];
        case 14: { // PULSE_WIDTH
            if (auto* vco = dynamic_cast<CompositeGenerator*>(find_by_tag("VCO"))) vco->pulse_osc().set_pulse_width(value);
            break;
        }
        default:
            break;
    }
}

void Voice::note_on(double frequency) {
    active_ = true;
    base_frequency_ = frequency;
    for (auto& entry : signal_chain_) entry.node->reset();
    lfo_->reset();
    if (filter_) filter_->reset();
    if (auto* vco = dynamic_cast<CompositeGenerator*>(find_by_tag("VCO"))) {
        vco->set_frequency(frequency);
    }
    if (auto* env = dynamic_cast<AdsrEnvelopeProcessor*>(find_by_tag("ENV"))) {
        env->gate_on();
    }
}

void Voice::note_off() {
    if (auto* env = dynamic_cast<AdsrEnvelopeProcessor*>(find_by_tag("ENV"))) {
        env->gate_off();
    }
    active_ = false;
}

bool Voice::is_active() const {
    if (active_) return true;
    for (const auto& entry : signal_chain_) {
        if (auto* env = dynamic_cast<AdsrEnvelopeProcessor*>(entry.node.get())) {
            return env->is_active();
        }
    }
    return false;
}

bool Voice::is_releasing() const {
    for (const auto& entry : signal_chain_) {
        if (auto* env = dynamic_cast<AdsrEnvelopeProcessor*>(entry.node.get())) {
            return !active_ && env->is_active();
        }
    }
    return false;
}

void Voice::reset() {
    for (auto& entry : signal_chain_) entry.node->reset();
    lfo_->reset();
    if (filter_) filter_->reset();
}

void Voice::apply_modulation() {
    auto* env_node = dynamic_cast<AdsrEnvelopeProcessor*>(find_by_tag("ENV"));
    float env_val = env_node ? env_node->get_level() : 0.0f;

    float l_buf[1];
    std::span<float> l_span(l_buf, 1);
    lfo_->pull(l_span);

    current_source_values_[static_cast<size_t>(ModulationSource::Envelope)] = env_val;
    current_source_values_[static_cast<size_t>(ModulationSource::LFO)] = l_buf[0];

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
}

void Voice::do_pull(std::span<float> output, const VoiceContext* context) {
    pull_mono(output, context);
}

void Voice::pull_mono(std::span<float> output, const VoiceContext* context) {
    apply_modulation();

    // --- Phase 15 graph executor (RT-SAFE: pool-borrowed buffers, no heap alloc) ---
    //
    // Each PORT_CONTROL node gets its own scratch buffer (borrowed from the
    // graph's BufferPool).  Audio nodes write in-place into `output`.  The
    // VCA step looks up its gain_cv source via connections_ (explicit) or by
    // falling back to the first PORT_CONTROL buffer encountered (legacy).
    //
    // Maximum simultaneously live control buffers equals the number of
    // PORT_CONTROL nodes in the chain — typically 1.

    static constexpr size_t kMaxCtrl = 8; // generous upper bound
    BufferPool::BufferPtr ctrl_ptrs[kMaxCtrl];
    std::span<float>      ctrl_spans[kMaxCtrl];
    const char*           ctrl_tags_arr[kMaxCtrl]; // raw pointer: entry.tag is stable
    size_t                num_ctrl = 0;

    // Pass 1: allocate scratch buffers for every PORT_CONTROL node.
    for (const auto& entry : signal_chain_) {
        if (entry.node->output_port_type() == PortType::PORT_CONTROL
                && num_ctrl < kMaxCtrl) {
            ctrl_ptrs[num_ctrl]  = graph_->borrow_buffer();
            ctrl_spans[num_ctrl] = std::span<float>(
                ctrl_ptrs[num_ctrl]->left.data(), output.size());
            ctrl_tags_arr[num_ctrl] = entry.tag.c_str();
            ++num_ctrl;
        }
    }

    // Helper: find the span for a given tag (linear scan, chain is tiny).
    auto find_ctrl = [&](const std::string& tag) -> std::span<float> {
        for (size_t i = 0; i < num_ctrl; ++i)
            if (tag == ctrl_tags_arr[i]) return ctrl_spans[i];
        return {};
    };

    // Pass 2: execute chain in order.
    bool audio_generated = false;
    size_t ctrl_pull_idx = 0; // which ctrl slot to fill next (matches chain order)

    for (auto& entry : signal_chain_) {
        if ([[maybe_unused]] auto* vca = dynamic_cast<VcaProcessor*>(entry.node.get()); vca != nullptr) {
            // Resolve gain_cv: use explicit connection if declared, else legacy fallback.
            std::span<float> gain_cv;
            for (const auto& conn : connections_) {
                if (conn.to_tag == entry.tag && conn.to_port == "gain_cv") {
                    gain_cv = find_ctrl(conn.from_tag);
                    break;
                }
            }
            if (gain_cv.empty() && num_ctrl > 0) {
                gain_cv = ctrl_spans[0]; // legacy: first control buffer is the envelope
            }
            // Filter sits between the last audio node and the VCA.
            if (audio_generated && filter_) filter_->pull(output, context);
            if (!gain_cv.empty()) {
                VcaProcessor::apply(output, gain_cv, base_amplitude_);
            }
        } else if (entry.node->output_port_type() == PortType::PORT_CONTROL) {
            entry.node->pull(ctrl_spans[ctrl_pull_idx], context);
            ++ctrl_pull_idx;
        } else {
            entry.node->pull(output, context);
            audio_generated = true;
        }
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

void Voice::connect(std::string from_tag, std::string from_port,
                    std::string to_tag,   std::string to_port) {
    connections_.push_back({std::move(from_tag), std::move(from_port),
                            std::move(to_tag),   std::move(to_port)});
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
    // Port-Type Rules:
    //   1. Last node must output PORT_AUDIO (chain output is always audio).
    //   2. No two consecutive PORT_CONTROL nodes (control after control is meaningless).
    auto port_of = [](const ChainEntry& e) {
        return e.node->output_port_type();
    };
    if (port_of(signal_chain_.back()) != PortType::PORT_AUDIO) {
        throw std::logic_error(
            "Voice::bake() failed: last node in signal_chain_ must output PORT_AUDIO");
    }
    for (size_t i = 1; i < signal_chain_.size(); ++i) {
        if (port_of(signal_chain_[i - 1]) == PortType::PORT_CONTROL &&
            port_of(signal_chain_[i])     == PortType::PORT_CONTROL) {
            throw std::logic_error(
                "Voice::bake() failed: consecutive PORT_CONTROL nodes at positions "
                + std::to_string(i - 1) + " and " + std::to_string(i));
        }
    }
    // Phase 15: validate named port connections
    static constexpr std::string_view kLifecyclePorts[] = {"gate_in", "trigger_in"};
    for (const auto& conn : connections_) {
        // Lifecycle ports are driven by VoiceContext — disallow explicit wiring.
        for (const auto& lp : kLifecyclePorts) {
            if (conn.to_port == lp || conn.from_port == lp) {
                throw std::logic_error(
                    "Voice::bake() failed: lifecycle port '" + std::string(lp)
                    + "' must not be wired via connect()");
            }
        }
        // Both nodes must be in the chain.
        auto* from = find_by_tag(conn.from_tag);
        auto* to   = find_by_tag(conn.to_tag);
        if (!from) throw std::logic_error(
            "Voice::bake() connection: unknown from_tag '" + conn.from_tag + "'");
        if (!to)   throw std::logic_error(
            "Voice::bake() connection: unknown to_tag '"   + conn.to_tag   + "'");
        // Both named ports must exist with matching types.
        const auto* fp = from->find_port(conn.from_port);
        const auto* tp = to->find_port(conn.to_port);
        if (!fp) throw std::logic_error(
            "Voice::bake() connection: port '" + conn.from_port
            + "' not declared on '" + conn.from_tag + "'");
        if (!tp) throw std::logic_error(
            "Voice::bake() connection: port '" + conn.to_port
            + "' not declared on '" + conn.to_tag + "'");
        if (fp->dir != PortDirection::OUT) throw std::logic_error(
            "Voice::bake() connection: '" + conn.from_port + "' on '"
            + conn.from_tag + "' is not an output port");
        if (tp->dir != PortDirection::IN) throw std::logic_error(
            "Voice::bake() connection: '" + conn.to_port + "' on '"
            + conn.to_tag + "' is not an input port");
        if (fp->type != tp->type) throw std::logic_error(
            "Voice::bake() connection: type mismatch between '"
            + conn.from_tag + "::" + conn.from_port + "' and '"
            + conn.to_tag   + "::" + conn.to_port   + "'");
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
