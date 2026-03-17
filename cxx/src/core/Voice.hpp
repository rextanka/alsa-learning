/**
 * @file Voice.hpp 
 * @brief Represents a single synthesizer voice.
 */

#ifndef AUDIO_VOICE_HPP
#define AUDIO_VOICE_HPP

#include "Processor.hpp"
#include "VcaProcessor.hpp"
#include "filter/FilterProcessor.hpp"
#include "oscillator/LfoProcessor.hpp"
#include "routing/CompositeGenerator.hpp"
#include "envelope/AdsrEnvelopeProcessor.hpp"
#include "AudioGraph.hpp"
#include "ModulationMatrix.hpp"
#include <memory>
#include <array>
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

    FilterProcessor* filter() { return filter_.get(); }
    LfoProcessor& lfo() { return *lfo_; }
    ModulationMatrix& matrix() { return matrix_; }

    void set_filter_type(std::unique_ptr<FilterProcessor> filter);
    void set_filter_type(int type); // 0: Moog, 1: Diode
    BufferPool::BufferPtr borrow_buffer() { return graph_->borrow_buffer(); }

    /**
     * @brief Set a modulation parameter (legacy integer-ID API).
     */
    void set_parameter(int param, float value);

    /**
     * @brief Set a named parameter on any chain node that recognises it.
     *
     * Tries all nodes in order via Processor::set_parameter(name, value).
     * Returns true if at least one node accepted the parameter.
     * Falls back to the legacy integer-ID table for well-known names.
     */
    bool set_named_parameter(const std::string& name, float value);

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
     * @brief Append a node to the signal chain and assign it a tag.
     *
     * Must only be called when the voice is Idle (not on the audio thread).
     * Invalidates the baked state — call bake() again before playing.
     */
    void add_processor(std::unique_ptr<Processor> p, std::string tag);

    /**
     * @brief Validate the chain and mark it ready for audio-thread use.
     *
     * Verifies that signal_chain_[0] is a Generator (CompositeGenerator).
     * Throws std::logic_error if validation fails.
     * Must be called after all add_processor() calls and before note_on().
     */
    void bake();

    /**
     * @brief Find the first chain node with the given tag. Returns nullptr if not found.
     *
     * Linear scan — O(n) on chain length. Called from control thread only.
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
    void apply_modulation();

    std::unique_ptr<FilterProcessor> filter_;
    std::unique_ptr<LfoProcessor> lfo_;
    std::unique_ptr<AudioGraph> graph_;
    
    ModulationMatrix matrix_;
    
    // Base parameters (anchors for modulation)
    double base_frequency_;
    float base_cutoff_;
    float base_resonance_;
    float base_amplitude_;

    // Current modulated parameters
    double current_frequency_;
    [[maybe_unused]] float current_amplitude_; // reserved for amplitude modulation routing

    int sample_rate_;
    float pan_; // -1.0 to 1.0

    // Temporary buffers for modulation sources
    std::array<float, static_cast<size_t>(ModulationSource::Count)> current_source_values_;

    uint32_t log_counter_;
    bool active_;

    // -------------------------------------------------------------------------
    // Phase 15: signal chain (populated via engine_add_module / add_processor / bake)
    // -------------------------------------------------------------------------
    struct ChainEntry {
        std::unique_ptr<Processor> node;
        std::string tag;
    };
    std::vector<ChainEntry> signal_chain_;
    std::vector<PortConnection> connections_; // Phase 15: named port wiring
    bool baked_ = false;
};

} // namespace audio

#endif // AUDIO_VOICE_HPP
