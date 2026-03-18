/**
 * @file Voice.hpp 
 * @brief Represents a single synthesizer voice.
 */

#ifndef AUDIO_VOICE_HPP
#define AUDIO_VOICE_HPP

#include "Processor.hpp"
#include "AudioGraph.hpp"
#include <memory>
#include <vector>
#include <string>
#include <string_view>

namespace audio {

/**
 * @brief A single synth voice.
 */
class Voice : public Processor {
public:
    explicit Voice(int sample_rate);

    void note_on(double frequency);
    void note_off();

    bool is_active() const;
    void reset() override;

    BufferPool::BufferPtr borrow_buffer() { return graph_->borrow_buffer(); }

    /**
     * @brief Set a named parameter on any chain node that recognises it.
     *
     * Handles Voice-level parameters ("osc_frequency", "amp_base") directly.
     * Routes bridge aliases ("vcf_cutoff", "amp_attack", etc.) to the tagged
     * node. Falls through to a general scan of all chain nodes via
     * apply_parameter for any other name.
     * Returns true if the parameter was accepted.
     */
    bool set_named_parameter(const std::string& name, float value);

    /**
     * @brief Set a named parameter on the specific tagged processor.
     *
     * Finds the node by @p tag and calls Processor::apply_parameter(name, value).
     * Returns true if the node was found and accepted the parameter.
     * Used by engine_load_patch for v2 tag-keyed parameter dispatch.
     */
    bool set_tag_parameter(const std::string& tag, const std::string& name, float value);

    void set_pan(float pan);
    float pan() const { return pan_; }

    /**
     * @brief Check if the voice is in the release stage.
     */
    bool is_releasing() const;

    // -------------------------------------------------------------------------
    // Phase 14: Dynamic Signal Chain API
    // -------------------------------------------------------------------------

    /**
     * @brief Append a node and assign it a tag.
     *
     * PORT_AUDIO nodes are appended to signal_chain_ (the audio path).
     * PORT_CONTROL nodes are appended to mod_sources_ (the CV/modulation path).
     *
     * Must only be called when the voice is Idle (not on the audio thread).
     * Invalidates the baked state — call bake() again before playing.
     */
    void add_processor(std::unique_ptr<Processor> p, std::string tag);

    /**
     * @brief Validate the chain and mark it ready for audio-thread use.
     *
     * Verifies that signal_chain_ is non-empty, that the first node outputs
     * PORT_AUDIO (the audio generator), and that the last node outputs PORT_AUDIO
     * (the chain sink). Throws std::logic_error if validation fails.
     * Must be called after all add_processor() calls and before note_on().
     */
    void bake();

    /**
     * @brief Find a node with the given tag in signal_chain_ or mod_sources_.
     * Returns nullptr if not found. Linear scan — O(n). Control thread only.
     */
    Processor* find_by_tag(std::string_view tag);

    // -------------------------------------------------------------------------
    // Phase 15: Named port connections (data-driven signal routing)
    // -------------------------------------------------------------------------

    /**
     * @brief A directed connection between two named ports on chain nodes.
     *
     * from_tag / from_port: source node tag and output port name.
     * to_tag   / to_port  : sink node tag and input port name.
     *
     * Lifecycle ports (gate_in, trigger_in) are driven by VoiceContext and
     * must NOT be wired via connect().
     */
    struct PortConnection {
        std::string from_tag;
        std::string from_port;
        std::string to_tag;
        std::string to_port;
    };

    /**
     * @brief Register a named port connection.
     *
     * Must be called before bake(). The connection is validated at bake() time.
     * Invalidates the baked state.
     */
    void connect(std::string from_tag, std::string from_port,
                 std::string to_tag,   std::string to_port);

    /**
     * @brief Read-only view of all registered connections.
     */
    const std::vector<PortConnection>& connections() const { return connections_; }

protected:
    void do_pull(std::span<float> output, const VoiceContext* context = nullptr) override;

public:
    /**
     * @brief Pull a mono signal from this voice.
     */
    void pull_mono(std::span<float> output, const VoiceContext* context = nullptr);

private:
    std::unique_ptr<AudioGraph> graph_;

    // Base parameters (anchors for CV modulation)
    double base_frequency_;
    float base_amplitude_;

    int sample_rate_;
    float pan_; // -1.0 to 1.0

    uint32_t log_counter_;
    bool active_;

    // -------------------------------------------------------------------------
    // Phase 15+: audio chain and modulation sources
    //
    // signal_chain_ — ordered list of PORT_AUDIO nodes (VCO → [VCF] → VCA).
    //                 The first node is the audio generator; the last is the sink.
    // mod_sources_  — unordered set of PORT_CONTROL generators (LFO, ADSR, …).
    //                 Pulled first each block; their output is routed to named
    //                 input ports on signal_chain_ nodes via connections_.
    // -------------------------------------------------------------------------
    struct ChainEntry {
        std::unique_ptr<Processor> node;
        std::string tag;
    };
    std::vector<ChainEntry> signal_chain_;
    std::vector<ChainEntry> mod_sources_;
    std::vector<PortConnection> connections_;
    bool baked_ = false;

    // Cached at bake() time: signal_chain_ nodes that declare a "kybd_cv" port.
    // Avoids a find_port() scan over all signal_chain_ nodes every block.
    std::vector<Processor*> kybd_cv_nodes_;
};

} // namespace audio

#endif // AUDIO_VOICE_HPP
