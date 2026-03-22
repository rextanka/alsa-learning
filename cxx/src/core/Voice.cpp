/**
 * @file Voice.cpp
 * @brief Implementation of the Voice class with Strictly Mono Signal Path.
 */

#include "Voice.hpp"
#include "VcaProcessor.hpp"
#include "routing/CompositeGenerator.hpp"
#include "envelope/AdsrEnvelopeProcessor.hpp" // needed for is_active()/is_releasing() dynamic_cast
#include "envelope/ADEnvelopeProcessor.hpp"   // needed for is_active()/is_releasing() dynamic_cast
#include "dynamics/EnvelopeFollowerProcessor.hpp" // needed for envelope→ctrl_spans injection in Pass 2
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <numeric>

namespace audio {

Voice::Voice(int sample_rate)
    : base_frequency_(440.0)
    , base_amplitude_(1.0f)
    , sample_rate_(sample_rate)
    , active_(false)
{
    graph_ = std::make_unique<AudioGraph>();
    log_counter_ = 0;
    AudioLogger::instance().log_event("SR_CHECK", static_cast<float>(sample_rate_));
}

void Voice::set_pan(float pan) {
    pan_param_.set_target(std::clamp(pan, -1.0f, 1.0f),
                          static_cast<int>(static_cast<float>(sample_rate_) * 0.010f));
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

bool Voice::set_tag_string_parameter(const std::string& tag, const std::string& name,
                                      const std::string& value) {
    auto* proc = find_by_tag(tag);
    if (proc) return proc->apply_string_parameter(name, value);
    return false;
}

void Voice::flush_all_processors() {
    for (auto& entry : signal_chain_) entry.node->flush_to_disk();
    for (auto& entry : mod_sources_)  entry.node->flush_to_disk();
}

void Voice::note_on(double frequency, float velocity) {
    active_ = true;
    base_frequency_ = frequency;
    for (auto& entry : signal_chain_)
        if (entry.node->reset_on_note_on()) entry.node->reset();
    for (auto& entry : mod_sources_) entry.node->reset();
    // signal_chain_[0] is guaranteed by bake() to be the audio generator.
    if (!signal_chain_.empty()) {
        signal_chain_.front().node->set_frequency(frequency);
    }
    // Broadcast velocity before on_note_on so CV sources (MIDI_CV) can latch it
    // into their pending state — velocity_cv will reflect it on the first block pull.
    for (auto& entry : mod_sources_) entry.node->on_note_velocity(velocity);
    for (auto& entry : mod_sources_) entry.node->on_note_on(frequency);
}

void Voice::note_off() {
    for (auto& entry : mod_sources_) entry.node->on_note_off();
    active_ = false;
}

bool Voice::is_active() const {
    if (active_) return true;
    // RT-SAFETY WARNING: dynamic_cast on the audio thread violates the no-RTTI policy
    // for the hot path. This is a known policy exception — VoiceManager calls is_active()
    // from the audio callback to determine voice stealing candidates.
    for (const auto& entry : mod_sources_) {
        if (auto* env = dynamic_cast<AdsrEnvelopeProcessor*>(entry.node.get()))
            return env->is_active();
        if (auto* env = dynamic_cast<ADEnvelopeProcessor*>(entry.node.get()))
            return env->is_active();
    }
    return false;
}

bool Voice::is_releasing() const {
    for (const auto& entry : mod_sources_) {
        if (auto* env = dynamic_cast<AdsrEnvelopeProcessor*>(entry.node.get()))
            return !active_ && env->is_active();
        if (auto* env = dynamic_cast<ADEnvelopeProcessor*>(entry.node.get()))
            return !active_ && env->is_active();
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
    // Advance pan interpolation so pan() reflects the current ramp position.
    pan_param_.advance(static_cast<int>(output.size()));

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

    // Secondary CV spans: one buffer per unique (tag, port) for multi-output CV sources
    // (e.g., MIDI_CV gate_cv, velocity_cv, aftertouch_cv). Populated inline during Pass 1
    // immediately after each mod_source is pulled, so subsequent nodes can inject them.
    static constexpr size_t kMaxSecondary = 8;
    struct SecondaryCtrl { const char* tag; const char* port; std::span<float> span; };
    BufferPool::BufferPtr secondary_ptrs[kMaxSecondary];
    SecondaryCtrl         secondary_ctrls[kMaxSecondary];
    size_t                num_secondary = 0;

    // Helper: find the ctrl span for a given (source tag, source port) pair.
    // Checks secondary spans first so multi-port CV sources (MIDI_CV gate_cv etc.)
    // override the primary do_pull buffer when from_port identifies a secondary output.
    // Falls back to the primary tag-only lookup when no secondary entry matches.
    auto find_ctrl = [&](const std::string& tag, std::string_view from_port = "") -> std::span<float> {
        if (!from_port.empty()) {
            for (size_t j = 0; j < num_secondary; ++j)
                if (tag == secondary_ctrls[j].tag && from_port == secondary_ctrls[j].port)
                    return secondary_ctrls[j].span;
        }
        for (size_t i = 0; i < num_ctrl; ++i)
            if (tag == ctrl_tags_arr[i]) return ctrl_spans[i];
        return {};
    };

    // Helper: mean value of a span (block-rate modulation).
    auto cv_mean = [](std::span<const float> s) -> float {
        if (s.empty()) return 0.0f;
        return std::accumulate(s.begin(), s.end(), 0.0f) / static_cast<float>(s.size());
    };

    // Pass 1: pull each mod source into its scratch buffer.
    // Before pulling, inject any inter-mod CV connections (mod_source → mod_source).
    // Sources with no incoming inter-mod connections pull cleanly; dependent sources
    // (CV_MIXER, INVERTER, MATHS, etc.) receive already-computed spans if the source
    // node was added to mod_sources_ before the consumer (ordering must be correct).
    for (auto& entry : mod_sources_) {
        if (num_ctrl >= kMaxCtrl) break;
        ctrl_ptrs[num_ctrl]  = graph_->borrow_buffer();
        ctrl_spans[num_ctrl] = std::span<float>(
            ctrl_ptrs[num_ctrl]->left.data(), output.size());
        ctrl_tags_arr[num_ctrl] = entry.tag.c_str();

        // Inject inter-mod CV inputs for this node (e.g. LFO → CV_MIXER:cv_in_1).
        // find_ctrl searches already-pulled sources (num_ctrl doesn't include current entry
        // yet), so only sources earlier in mod_sources_ are available — ordering matters.
        // Pass from_port so secondary spans (MIDI_CV gate_cv etc.) are resolved correctly.
        for (const auto& conn : connections_) {
            if (conn.to_tag != entry.tag) continue;
            auto cv = find_ctrl(conn.from_tag, conn.from_port);
            if (!cv.empty()) entry.node->inject_cv(conn.to_port, cv);
        }

        entry.node->pull(ctrl_spans[num_ctrl], context);

        // For multi-port CV sources (e.g., MIDI_CV), register a secondary buffer
        // for each outgoing connection whose from_port provides a distinct named value.
        // Must happen immediately after pull so the next mod_source (e.g., CV_MIXER)
        // can inject the correct span during its inter-mod injection pass.
        for (const auto& conn : connections_) {
            if (conn.from_tag != entry.tag) continue;
            if (!entry.node->provides_named_cv(conn.from_port)) continue;
            if (num_secondary >= kMaxSecondary) break;
            // Dedup: one buffer per unique (tag, port).
            bool already = false;
            for (size_t j = 0; j < num_secondary; ++j)
                if (secondary_ctrls[j].tag == entry.tag.c_str() &&
                    std::string_view(secondary_ctrls[j].port) == conn.from_port)
                    { already = true; break; }
            if (already) continue;
            secondary_ptrs[num_secondary] = graph_->borrow_buffer();
            auto sec = std::span<float>(secondary_ptrs[num_secondary]->left.data(), output.size());
            std::fill(sec.begin(), sec.end(), entry.node->get_cv_output(conn.from_port));
            secondary_ctrls[num_secondary] = {entry.tag.c_str(), conn.from_port.c_str(), sec};
            ++num_secondary;
        }

        ++num_ctrl;
    }

    // Auto-inject keyboard tracking CV to cached kybd_cv nodes (populated at bake time).
    // Computed as 1V/oct relative to C4 (261.63 Hz). Combines additively with cutoff_cv
    // in the filter's apply_parameter("kybd_cv") handler.
    if (!kybd_cv_nodes_.empty()) {
        static constexpr float kC4Hz = 261.63f;
        const float kybd_cv = static_cast<float>(
            std::log2(base_frequency_ / static_cast<double>(kC4Hz)));
        for (auto* node : kybd_cv_nodes_)
            node->apply_parameter("kybd_cv", kybd_cv);
    }

    // --- Audio bus: pre-pull audio nodes that supply secondary audio inputs.
    //
    // An audio_source is any signal_chain_ node whose audio_out is explicitly
    // connected to a non-primary audio input on another node (i.e., to_port is
    // not "audio_in" — covers audio_in_a, audio_in_b, fm_in, etc.).
    // Those nodes pull into their own borrowed buffer so the receiving node can
    // inject the correct signal via inject_audio() before its own do_pull().
    // -------------------------------------------------------------------------
    static constexpr size_t kMaxAudioBus = 8;
    BufferPool::BufferPtr audio_bus_ptrs[kMaxAudioBus];
    std::span<float>      audio_bus_spans[kMaxAudioBus];
    const char*           audio_bus_tags[kMaxAudioBus];
    size_t                num_audio_bus = 0;

    auto is_audio_source = [&](const std::string& tag) -> bool {
        for (const auto& conn : connections_) {
            if (conn.from_tag == tag
                && conn.from_port.size() >= 9
                && conn.from_port.compare(0, 9, "audio_out") == 0
                && conn.to_port != "audio_in") {
                return true;
            }
        }
        return false;
    };

    auto find_audio_bus = [&](const std::string& tag) -> std::span<float> {
        for (size_t i = 0; i < num_audio_bus; ++i)
            if (tag == audio_bus_tags[i]) return audio_bus_spans[i];
        return {};
    };

    for (auto& entry : signal_chain_) {
        if (num_audio_bus >= kMaxAudioBus) break;
        if (is_audio_source(entry.tag)) {
            audio_bus_ptrs[num_audio_bus]  = graph_->borrow_buffer();
            audio_bus_spans[num_audio_bus] = std::span<float>(
                audio_bus_ptrs[num_audio_bus]->left.data(), output.size());
            audio_bus_tags[num_audio_bus]  = entry.tag.c_str();
            ++num_audio_bus;
        }
    }

    // Pass 2: execute audio chain. signal_chain_ contains only PORT_AUDIO nodes.
    for (auto& entry : signal_chain_) {
        if ([[maybe_unused]] auto* vca = dynamic_cast<VcaProcessor*>(entry.node.get()); vca != nullptr) {
            // Resolve gain_cv via explicit connection only — no implicit fallback.
            std::span<float> gain_cv;
            for (const auto& conn : connections_) {
                if (conn.to_tag == entry.tag && conn.to_port == "gain_cv") {
                    gain_cv = find_ctrl(conn.from_tag, conn.from_port);
                    break;
                }
            }

            // Phase 26: resolve initial_gain_cv — overrides initial_gain parameter
            // when connected. Use first sample as the block-constant floor value.
            std::span<float> initial_gain_cv;
            for (const auto& conn : connections_) {
                if (conn.to_tag == entry.tag && conn.to_port == "initial_gain_cv") {
                    initial_gain_cv = find_ctrl(conn.from_tag, conn.from_port);
                    break;
                }
            }
            const float gain_floor = initial_gain_cv.empty()
                                     ? vca->initial_gain()
                                     : initial_gain_cv[0];
            const float scale = gain_floor * base_amplitude_;

            if (!gain_cv.empty()) {
                VcaProcessor::apply(output, gain_cv, scale, vca->response_curve());
            } else {
                // No modulation source connected — apply scale directly.
                for (auto& s : output) s *= scale;
            }

        } else {
            // Before pulling: inject any explicit audio inputs from the audio bus.
            // This handles RING_MOD (audio_in_a/audio_in_b), fm_in on VCO/filters, etc.
            for (const auto& conn : connections_) {
                if (conn.to_tag != entry.tag) continue;
                if (conn.to_port == "audio_in") continue; // primary inline — skip
                if (conn.to_port.size() < 8 || conn.to_port.compare(0, 8, "audio_in") != 0) continue;
                // Named secondary audio input (audio_in_a, audio_in_b, fm_in skipped below)
                auto abus = find_audio_bus(conn.from_tag);
                if (!abus.empty())
                    entry.node->inject_audio(conn.to_port, std::span<const float>(abus));
            }
            // Inject fm_in connections separately (port name doesn't start with "audio_in")
            for (const auto& conn : connections_) {
                if (conn.to_tag != entry.tag) continue;
                if (conn.to_port != "fm_in") continue;
                auto abus = find_audio_bus(conn.from_tag);
                if (!abus.empty())
                    entry.node->inject_audio(conn.to_port, std::span<const float>(abus));
            }
            // Inject sync_in: retrieve sync_out secondary output from the source node.
            // The source must be earlier in signal_chain_ (already pulled this block).
            for (const auto& conn : connections_) {
                if (conn.to_tag != entry.tag) continue;
                if (conn.to_port != "sync_in") continue;
                auto* src = find_by_tag(conn.from_tag);
                if (src) {
                    auto sync_data = src->get_secondary_output("sync_out");
                    if (!sync_data.empty())
                        entry.node->inject_audio("sync_in", sync_data);
                }
            }

            // PORT_AUDIO node: apply any incoming CV connections first.
            for (const auto& conn : connections_) {
                if (conn.to_tag != entry.tag) continue;
                auto cv = find_ctrl(conn.from_tag, conn.from_port);
                if (cv.empty()) continue;

                // Port-name convention: a source port ending in "_inv" (e.g.
                // LFO:control_out_inv) is the inverted complement of the primary
                // output. Negate the block-mean value before dispatching.
                const float sign = conn.from_port.ends_with("_inv") ? -1.0f : 1.0f;

                if (conn.to_port == "pitch_base_cv") {
                    // Absolute 1 V/oct pitch from MIDI_CV. C4 = 0 V; +1 V = one octave up.
                    // f = 261.63 Hz × 2^cv  (261.63 = middle C, MIDI 60).
                    // Replaces base_frequency_ for this block — pitch_cv can still add offset.
                    const float cv_v = sign * cv_mean(cv);
                    const double abs_freq = 261.63 * std::pow(2.0, static_cast<double>(cv_v));
                    base_frequency_ = abs_freq > 20.0 ? abs_freq : 20.0;
                    entry.node->set_frequency(base_frequency_);

                } else if (conn.to_port == "pitch_cv") {
                    const float mod = sign * cv_mean(cv);
                    const double mod_freq = base_frequency_ * std::pow(2.0, static_cast<double>(mod));
                    entry.node->set_frequency(mod_freq > 20.0 ? mod_freq : base_frequency_);

                } else if (conn.to_port == "pwm_cv") {
                    // Map bipolar CV → pulse width [0.01, 0.49], then drive via
                    // apply_parameter so do_pull reads the SmoothedParam correctly
                    // (direct set_pulse_width was overwritten by do_pull each block).
                    const float pw = std::clamp(0.5f + sign * cv_mean(cv) * 0.5f, 0.01f, 0.49f);
                    entry.node->apply_parameter("pulse_width", pw);

                } else {
                    // General CV dispatch: route any named port to the target node
                    // via apply_parameter. Filters handle cutoff_cv / res_cv internally
                    // (exponential scaling, base preservation, etc.).
                    auto* target = find_by_tag(conn.to_tag);
                    if (target) target->apply_parameter(conn.to_port, sign * cv_mean(cv));
                }
            }

            // Pull into audio_bus if this node is an audio source; else inline.
            auto abus = find_audio_bus(entry.tag);
            if (!abus.empty()) {
                // If this node has a primary inline audio input (audio_in from a non-bus
                // predecessor), copy the current inline output into abus first so the
                // node can process in-place (e.g. VCF1 in NOISE1→VCF1→MIX.audio_in_1).
                for (const auto& conn : connections_) {
                    if (conn.to_tag == entry.tag && conn.to_port == "audio_in"
                        && find_audio_bus(conn.from_tag).empty()) {
                        std::copy(output.begin(), output.end(), abus.begin());
                        break;
                    }
                }
                entry.node->pull(abus, context);
            } else {
                entry.node->pull(output, context);
            }

            // EnvelopeFollower special case: after pull(), capture the computed
            // envelope value into ctrl_spans[] so downstream CV consumers (VCA
            // gain_cv, filter cutoff_cv, etc.) can resolve it via find_ctrl().
            // The node stays in signal_chain_ as a transparent audio passthrough;
            // the ctrl entry makes its envelope_out available to the CV dispatch.
            if (num_ctrl < kMaxCtrl) {
                if (auto* ef = dynamic_cast<EnvelopeFollowerProcessor*>(entry.node.get())) {
                    ctrl_ptrs[num_ctrl]  = graph_->borrow_buffer();
                    ctrl_spans[num_ctrl] = std::span<float>(
                        ctrl_ptrs[num_ctrl]->left.data(), output.size());
                    std::fill(ctrl_spans[num_ctrl].begin(), ctrl_spans[num_ctrl].end(),
                              ef->get_envelope());
                    ctrl_tags_arr[num_ctrl] = entry.tag.c_str();
                    ++num_ctrl;
                }
            }
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
    {
        auto& back = signal_chain_.back();
        // A SINK has audio_in but no audio_out (Phase 27C: AUDIO_OUTPUT, AUDIO_FILE_WRITER).
        // Allow it as the last chain node — inline audio from the predecessor passes through.
        const bool is_sink = std::any_of(
            back.node->ports().begin(), back.node->ports().end(),
            [](const PortDescriptor& p) {
                return p.type == PortType::PORT_AUDIO && p.dir == PortDirection::IN;
            }) &&
            std::none_of(
            back.node->ports().begin(), back.node->ports().end(),
            [](const PortDescriptor& p) {
                return p.type == PortType::PORT_AUDIO && p.dir == PortDirection::OUT;
            });
        if (!is_sink && back.node->output_port_type() != PortType::PORT_AUDIO) {
            throw std::logic_error(
                "Voice::bake() failed: last node must output PORT_AUDIO or be a SINK "
                "(audio_in with no audio_out)");
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

    // Cache signal_chain_ nodes that declare a "kybd_cv" port so the hot path
    // can iterate directly without calling find_port() on every block.
    kybd_cv_nodes_.clear();
    for (auto& entry : signal_chain_) {
        if (entry.node->find_port("kybd_cv"))
            kybd_cv_nodes_.push_back(entry.node.get());
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
