/**
 * @file Voice.cpp
 * @brief Implementation of the Voice class with Strictly Mono Signal Path.
 */

#include "Voice.hpp"
#include "VcaProcessor.hpp"
#include "routing/CompositeGenerator.hpp"
#include "envelope/AdsrEnvelopeProcessor.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <numeric>

namespace audio {

Voice::Voice(int sample_rate)
    : base_frequency_(440.0)
    , base_amplitude_(1.0f)
    , sample_rate_(sample_rate)
    , pan_(0.0f)
    , active_(false)
{
    graph_ = std::make_unique<AudioGraph>();
    log_counter_ = 0;
    AudioLogger::instance().log_event("SR_CHECK", static_cast<float>(sample_rate_));
}

void Voice::set_pan(float pan) {
    pan_ = std::clamp(pan, -1.0f, 1.0f);
}

bool Voice::set_named_parameter(const std::string& name, float value) {
    // Voice-level parameters (not stored on any processor node)
    if (name == "osc_frequency") {
        base_frequency_ = static_cast<double>(value);
        if (!signal_chain_.empty())
            signal_chain_.front().node->set_frequency(base_frequency_);
        return true;
    }
    if (name == "amp_base") {
        base_amplitude_ = std::clamp(value, 0.0f, 1.0f);
        return true;
    }
    // Bridge parameter aliases: route to specific tagged nodes
    if (name == "vcf_cutoff")  return set_tag_parameter("VCF", "cutoff",    std::max(20.0f, value));
    if (name == "vcf_res")     return set_tag_parameter("VCF", "resonance", std::clamp(value, 0.0f, 1.0f));
    if (name == "amp_attack")  return set_tag_parameter("ENV", "attack",    value);
    if (name == "amp_decay")   return set_tag_parameter("ENV", "decay",     value);
    if (name == "amp_sustain") return set_tag_parameter("ENV", "sustain",   value);
    if (name == "amp_release") return set_tag_parameter("ENV", "release",   value);
    // General dispatch: try each node in order
    for (auto& entry : signal_chain_) {
        if (entry.node->apply_parameter(name, value)) return true;
    }
    for (auto& entry : mod_sources_) {
        if (entry.node->apply_parameter(name, value)) return true;
    }
    return false;
}

bool Voice::set_tag_parameter(const std::string& tag, const std::string& name, float value) {
    auto* proc = find_by_tag(tag);
    if (proc) return proc->apply_parameter(name, value);
    return false;
}

void Voice::note_on(double frequency) {
    active_ = true;
    base_frequency_ = frequency;
    for (auto& entry : signal_chain_) entry.node->reset();
    for (auto& entry : mod_sources_) entry.node->reset();
    // signal_chain_[0] is guaranteed by bake() to be the audio generator.
    if (!signal_chain_.empty()) {
        signal_chain_.front().node->set_frequency(frequency);
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
    // from the audio callback to determine voice stealing candidates.
    for (const auto& entry : mod_sources_) {
        if (auto* env = dynamic_cast<AdsrEnvelopeProcessor*>(entry.node.get())) {
            return env->is_active();
        }
    }
    return false;
}

bool Voice::is_releasing() const {
    for (const auto& entry : mod_sources_) {
        if (auto* env = dynamic_cast<AdsrEnvelopeProcessor*>(entry.node.get())) {
            return !active_ && env->is_active();
        }
    }
    return false;
}

void Voice::reset() {
    for (auto& entry : signal_chain_) entry.node->reset();
    for (auto& entry : mod_sources_) entry.node->reset();
}

void Voice::do_pull(std::span<float> output, const VoiceContext* context) {
    pull_mono(output, context);
}

void Voice::pull_mono(std::span<float> output, const VoiceContext* context) {
    // --- Graph executor (RT-SAFE: pool-borrowed buffers, no heap alloc) ---
    //
    // Pass 1: pull all mod_sources (PORT_CONTROL generators: LFO, ADSR, …) into
    //         scratch buffers. These are CV signals that modulate the audio chain.
    // Pass 2: execute signal_chain_ in order (all PORT_AUDIO nodes). Before each
    //         node, apply any incoming CV connections (pitch_cv, pwm_cv, cutoff_cv).
    //         VCA is handled specially: filter is applied before it, then gain_cv.

    static constexpr size_t kMaxCtrl = 8;
    BufferPool::BufferPtr ctrl_ptrs[kMaxCtrl];
    std::span<float>      ctrl_spans[kMaxCtrl];
    const char*           ctrl_tags_arr[kMaxCtrl];
    size_t                num_ctrl = 0;

    // Pass 1: pull each mod source into its scratch buffer.
    for (auto& entry : mod_sources_) {
        if (num_ctrl >= kMaxCtrl) break;
        ctrl_ptrs[num_ctrl]  = graph_->borrow_buffer();
        ctrl_spans[num_ctrl] = std::span<float>(
            ctrl_ptrs[num_ctrl]->left.data(), output.size());
        ctrl_tags_arr[num_ctrl] = entry.tag.c_str();
        entry.node->pull(ctrl_spans[num_ctrl], context);
        ++num_ctrl;
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

    // Pass 2: execute audio chain. signal_chain_ contains only PORT_AUDIO nodes.
    for (auto& entry : signal_chain_) {
        if ([[maybe_unused]] auto* vca = dynamic_cast<VcaProcessor*>(entry.node.get()); vca != nullptr) {
            // Resolve gain_cv via explicit connection only — no implicit fallback.
            std::span<float> gain_cv;
            for (const auto& conn : connections_) {
                if (conn.to_tag == entry.tag && conn.to_port == "gain_cv") {
                    gain_cv = find_ctrl(conn.from_tag);
                    break;
                }
            }

            if (!gain_cv.empty()) {
                VcaProcessor::apply(output, gain_cv, base_amplitude_);
            } else {
                // No modulation source connected — apply base_amplitude_ at unity gain
                // so the output level is defined regardless of patch topology.
                for (auto& s : output) s *= base_amplitude_;
            }

        } else {
            // PORT_AUDIO node: apply any incoming CV connections first.
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

                } else {
                    // General CV dispatch: route any named port to the target node
                    // via apply_parameter. Filters handle cutoff_cv / res_cv internally
                    // (exponential scaling, base preservation, etc.).
                    auto* target = find_by_tag(conn.to_tag);
                    if (target) target->apply_parameter(conn.to_port, cv_mean(cv));
                }
            }
            entry.node->pull(output, context);
        }
    }
}

// =============================================================================
// Phase 14: Dynamic Signal Chain API
// =============================================================================

void Voice::add_processor(std::unique_ptr<Processor> p, std::string tag) {
    p->set_tag(tag);
    if (p->output_port_type() == PortType::PORT_CONTROL) {
        mod_sources_.push_back({std::move(p), std::move(tag)});
    } else {
        signal_chain_.push_back({std::move(p), std::move(tag)});
    }
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
    // signal_chain_ contains only PORT_AUDIO nodes (add_processor routes PORT_CONTROL
    // nodes to mod_sources_). Enforce generator-first and sink-last rules.
    if (signal_chain_.front().node->output_port_type() != PortType::PORT_AUDIO) {
        throw std::logic_error(
            "Voice::bake() failed: first node in signal_chain_ must output PORT_AUDIO");
    }
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
    for (auto& entry : mod_sources_) {
        if (entry.tag == tag) return entry.node.get();
    }
    return nullptr;
}

} // namespace audio
