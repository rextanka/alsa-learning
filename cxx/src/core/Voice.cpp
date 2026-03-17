/**
 * @file Voice.cpp
 * @brief Implementation of the Voice class with Strictly Mono Signal Path.
 */

#include "Voice.hpp"
#include "filter/MoogLadderProcessor.hpp"
#include "filter/DiodeLadderProcessor.hpp"
#include "routing/CompositeGenerator.hpp"
#include <cmath>
#include <stdexcept>
#include <numeric>

namespace audio {

Voice::Voice(int sample_rate)
    : base_frequency_(440.0)
    , base_cutoff_(20000.0f)
    , base_resonance_(0.4f)
    , base_amplitude_(1.0f)
    , sample_rate_(sample_rate)
    , pan_(0.0f)
    , active_(false)
{
    graph_ = std::make_unique<AudioGraph>();
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

bool Voice::set_named_parameter(const std::string& name, float value) {
    // Try the virtual Processor::apply_parameter() on each chain node.
    for (auto& entry : signal_chain_) {
        if (entry.node->apply_parameter(name, value)) return true;
    }
    return false;
}

void Voice::note_on(double frequency) {
    active_ = true;
    base_frequency_ = frequency;
    for (auto& entry : signal_chain_) entry.node->reset();
    if (filter_) filter_->reset();
    // Dispatch frequency to the first PORT_AUDIO node (the audio generator).
    // PORT_CONTROL nodes (LFO etc.) may precede it in the chain — skip them.
    for (auto& entry : signal_chain_) {
        if (entry.node->output_port_type() == PortType::PORT_AUDIO) {
            entry.node->set_frequency(frequency);
            break;
        }
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
    // RT-SAFETY WARNING: dynamic_cast on the audio thread violates the no-RTTI policy
    // for the hot path. This is a known policy exception — VoiceManager calls is_active()
    // from the audio callback to determine voice stealing candidates. A Phase 16 fix
    // would replace this with a typed active_state enum cached on the Voice.
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
    if (filter_) filter_->reset();
}

void Voice::do_pull(std::span<float> output, const VoiceContext* context) {
    pull_mono(output, context);
}

void Voice::pull_mono(std::span<float> output, const VoiceContext* context) {
    // --- Phase 16 graph executor (RT-SAFE: pool-borrowed buffers, no heap alloc) ---
    //
    // Pass 1: allocate scratch buffers for every PORT_CONTROL node in the chain.
    // Pass 2: execute chain in order:
    //   - PORT_CONTROL nodes are pulled into their scratch spans.
    //   - Before pulling each PORT_AUDIO node, any named PORT_CONTROL connections
    //     targeting it are applied (pitch_cv, pwm_cv; gain_cv is handled by VCA).
    //   - VCA resolves gain_cv via explicit connection or falls back to ctrl_spans[0].

    static constexpr size_t kMaxCtrl = 8;
    BufferPool::BufferPtr ctrl_ptrs[kMaxCtrl];
    std::span<float>      ctrl_spans[kMaxCtrl];
    const char*           ctrl_tags_arr[kMaxCtrl];
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

    // Helper: find the ctrl span for a given source tag.
    auto find_ctrl = [&](const std::string& tag) -> std::span<float> {
        for (size_t i = 0; i < num_ctrl; ++i)
            if (tag == ctrl_tags_arr[i]) return ctrl_spans[i];
        return {};
    };

    // Helper: mean value of a span (block-rate modulation).
    auto cv_mean = [](std::span<const float> s) -> float {
        if (s.empty()) return 0.0f;
        return std::accumulate(s.begin(), s.end(), 0.0f) / static_cast<float>(s.size());
    };

    // Pass 2: execute chain in order.
    bool audio_generated = false;
    size_t ctrl_pull_idx = 0;

    for (auto& entry : signal_chain_) {
        if ([[maybe_unused]] auto* vca = dynamic_cast<VcaProcessor*>(entry.node.get()); vca != nullptr) {
            // Resolve gain_cv via explicit connection or fallback to first ctrl buffer.
            std::span<float> gain_cv;
            for (const auto& conn : connections_) {
                if (conn.to_tag == entry.tag && conn.to_port == "gain_cv") {
                    gain_cv = find_ctrl(conn.from_tag);
                    break;
                }
            }
            if (gain_cv.empty() && num_ctrl > 0)
                gain_cv = ctrl_spans[0];

            if (audio_generated && filter_) filter_->pull(output, context);
            if (!gain_cv.empty())
                VcaProcessor::apply(output, gain_cv, base_amplitude_);

        } else if (entry.node->output_port_type() == PortType::PORT_CONTROL) {
            entry.node->pull(ctrl_spans[ctrl_pull_idx], context);
            ++ctrl_pull_idx;

        } else {
            // PORT_AUDIO node: apply any incoming PORT_CONTROL connections first.
            for (const auto& conn : connections_) {
                if (conn.to_tag != entry.tag) continue;
                auto cv = find_ctrl(conn.from_tag);
                if (cv.empty()) continue;

                if (conn.to_port == "pitch_cv") {
                    const float mod = cv_mean(cv);
                    const double mod_freq = base_frequency_ * std::pow(2.0, static_cast<double>(mod));
                    entry.node->set_frequency(mod_freq > 20.0 ? mod_freq : base_frequency_);

                } else if (conn.to_port == "pwm_cv") {
                    const float pw = std::clamp(0.5f + cv_mean(cv) * 0.5f, 0.01f, 0.49f);
                    if (auto* vco = dynamic_cast<CompositeGenerator*>(entry.node.get()))
                        vco->pulse_osc().set_pulse_width(pw);

                } else if (conn.to_port == "cutoff_cv" && filter_) {
                    const float mod_cutoff = std::max(20.0f,
                        base_cutoff_ * std::pow(2.0f, cv_mean(cv)));
                    filter_->set_cutoff(mod_cutoff);
                }
            }
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
    // Port-Type Rule: last node must output PORT_AUDIO (chain output is always audio).
    // Note: PORT_CONTROL nodes (LFO, etc.) may appear before the first PORT_AUDIO generator.
    if (signal_chain_.back().node->output_port_type() != PortType::PORT_AUDIO) {
        throw std::logic_error(
            "Voice::bake() failed: last node in signal_chain_ must output PORT_AUDIO");
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
