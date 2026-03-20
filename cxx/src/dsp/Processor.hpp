/**
 * @file Processor.hpp 
 * @brief Base class for digital signal processing (DSP) components.
 * 
 * This file follows the project rules defined in .cursorrules:
 * - Separation of Concerns: Core DSP logic (oscillators, envelopes, filters) 
 *   must be strictly separated from hardware/OS audio code.
 * - Modern C++: Target C++20/23 for all new code.
 * - Pull Model: Output pulls from graph, processors pull from inputs.
 */

#ifndef PROCESSOR_HPP
#define PROCESSOR_HPP

#include <span>
#include <vector>
#include <algorithm>
#include <chrono>
#include <cstddef>
#include <string>
#include <string_view>
#include "InputSource.hpp"
#include "VoiceContext.hpp"
#include "PerformanceProfiler.hpp"
#include "AudioBuffer.hpp"

namespace audio {

/**
 * @brief Port type classification for typed signal routing (Phase 14/15).
 *
 * PORT_AUDIO — carries audio-rate samples in [-1, 1].
 * PORT_CONTROL — carries control-rate signals, unipolar [0,1] or bipolar [-1,1].
 * Both types run at the same sample rate; the distinction is semantic and is validated
 * at bake() time to prevent misrouted connections.
 */
enum class PortType {
    PORT_AUDIO,
    PORT_CONTROL
};

// C++20: bring enumerators into namespace audio so subclasses can write
// PORT_AUDIO / PORT_CONTROL without the PortType:: prefix.
using enum PortType;

/** @brief Port direction — input or output on a module. */
enum class PortDirection { IN, OUT };

/**
 * @brief Named port descriptor (Phase 15).
 *
 * Each Processor subclass calls declare_port() in its constructor for every
 * port it owns. The ModuleRegistry aggregates these at library load time.
 *
 * unipolar: true if PORT_CONTROL range is [0,1]; false if bipolar [-1,1].
 *           Ignored for PORT_AUDIO ports.
 */
struct PortDescriptor {
    std::string   name;
    PortType      type;
    PortDirection dir;
    bool          unipolar    = false; ///< true → [0,1]; false → [-1,1] (PORT_CONTROL only)
    std::string   description = "";    ///< usage note for introspection API (Phase 26)
};

/**
 * @brief Parameter descriptor (Phase 15).
 *
 * Each Processor subclass calls declare_parameter() in its constructor for
 * every user-controllable parameter it exposes. Queryable via the C API.
 */
struct ParameterDescriptor {
    std::string name;        ///< internal label (e.g. "cutoff")
    std::string label;       ///< human-readable (e.g. "Cutoff Frequency")
    float       min  = 0.0f;
    float       max  = 1.0f;
    float       def  = 0.0f; ///< default value
    bool        logarithmic = false;
    std::string description = ""; ///< usage note for introspection API (Phase 26)
};

/**
 * @brief Base class for audio processing units (Pull Model).
 * 
 * DSP components (oscillators, envelopes, filters, etc.) inherit from this class
 * to provide a unified processing interface. Processors can pull from multiple
 * input sources and implement the pull model architecture.
 * 
 * Key features:
 * - Pull model: Processors pull data from their inputs
 * - Performance profiling: Optional nanosecond-precision timing
 * - Voice context: Query pattern for accessing voice parameters
 */
class Processor : public InputSource {
public:
    /**
     * @brief Performance metrics structure.
     * 
     * Only populated when AUDIO_ENABLE_PROFILING is defined.
     */
    struct PerformanceMetrics {
        std::chrono::nanoseconds last_execution_time{0};
        std::chrono::nanoseconds max_execution_time{0};
        size_t total_blocks_processed{0};
    };

    virtual ~Processor() = default;

    /**
     * @brief Pull data into output span (Pull Model).
     * 
     * This processor will pull from its inputs if needed, then process
     * the data and fill the output span.
     * 
     * @param output Output buffer to fill (mono, block-based)
     * @param voice_context Optional voice context for parameter querying
     */
    void pull(std::span<float> output, const VoiceContext* voice_context = nullptr) override {
#if AUDIO_ENABLE_PROFILING
        profiler_.start();
#endif
        
        do_pull(output, voice_context);
        
#if AUDIO_ENABLE_PROFILING
        profiler_.stop();
#endif
    }

    /**
     * @brief Pull data into stereo AudioBuffer (Pull Model).
     * 
     * Default implementation calls the mono pull() if only mono is supported.
     * Subclasses can override for native stereo processing.
     */
    virtual void pull(AudioBuffer& output, const VoiceContext* voice_context = nullptr) {
#if AUDIO_ENABLE_PROFILING
        profiler_.start();
#endif
        
        do_pull(output, voice_context);
        
#if AUDIO_ENABLE_PROFILING
        profiler_.stop();
#endif
    }

    /**
     * @brief Add an input source that this processor pulls from.
     * 
     * @param input Input source to add
     */
    void add_input(InputSource* input) {
        if (input != nullptr) {
            inputs_.push_back(input);
        }
    }

    /**
     * @brief Remove an input source.
     * 
     * @param input Input source to remove
     */
    void remove_input(InputSource* input) {
        inputs_.erase(
            std::remove(inputs_.begin(), inputs_.end(), input),
            inputs_.end()
        );
    }

    /**
     * @brief Reset internal state (for voice stealing).
     */
    virtual void reset() = 0;

    /**
     * @brief Whether this processor should be reset on each note_on.
     *
     * Override to return false for stateful processors (filters, delay lines)
     * that should maintain state across notes — e.g. a TB-303 filter that
     * builds resonance continuously between notes.
     */
    virtual bool reset_on_note_on() const { return true; }

    /**
     * @brief Node tag for signal chain discovery (Phase 14).
     *
     * Tags are assigned by engine_add_module() (e.g. "VCO", "VCF", "ENV", "VCA").
     * Used by Voice::find_by_tag() to locate nodes for parameter routing.
     */
    void set_tag(std::string_view tag) { tag_ = tag; }
    std::string_view tag() const { return tag_; }

    /**
     * @brief Declared output port type for bake() chain-level validation (Phase 14).
     * Default: PORT_AUDIO. Override to PORT_CONTROL for control-rate generators (e.g. ADSR).
     *
     * For named-port validation (Phase 15) use declare_port() instead.
     */
    virtual PortType output_port_type() const { return PortType::PORT_AUDIO; }

    /**
     * @brief Set the base oscillator frequency (Hz). No-op for non-generator nodes.
     *
     * Override in generator processors (e.g. CompositeGenerator, DrawbarOrganProcessor)
     * so that Voice::note_on() can dispatch without a dynamic_cast.
     */
    virtual void set_frequency(double /*freq*/) {}

    /**
     * @brief Apply a named parameter by string key.
     *
     * Returns true if the parameter was recognised and applied.
     * Default implementation is a no-op (returns false).
     * Override in module processors (e.g. DrawbarOrganProcessor).
     */
    virtual bool apply_parameter(const std::string& /*name*/, float /*value*/) { return false; }

    /**
     * @brief Inject a named CV input span before the next do_pull() call.
     *
     * Called by the Voice graph executor when a mod-to-mod connection routes
     * another mod_source's output to this processor's named input port.
     * The injected span is valid for the current block only — processors must
     * not store the span pointer across blocks.
     *
     * Default: no-op. Override in processors that consume CV inputs
     * (CV_MIXER, CV_SPLITTER, MATHS, SAMPLE_HOLD, INVERTER, ADSR ext_gate_in).
     */
    virtual void inject_cv(std::string_view /*port_name*/, std::span<const float> /*cv*/) {}

    /**
     * @brief Inject a named audio input span before the next do_pull() call.
     *
     * Called by the Voice graph executor for multi-audio-input nodes (RING_MOD,
     * filter fm_in, VCO fm_in). The injected span is valid for the current block
     * only — processors must not store the span pointer across blocks.
     *
     * Default: no-op. Override in processors that consume secondary audio inputs
     * (RING_MOD audio_in_a/audio_in_b, COMPOSITE_GENERATOR fm_in,
     *  MOOG_FILTER/DIODE_FILTER fm_in, AUDIO_SPLITTER audio_in).
     */
    virtual void inject_audio(std::string_view /*port_name*/, std::span<const float> /*audio*/) {}

    /**
     * @brief Notification that a note-on event has occurred.
     *
     * Called by Voice::note_on() for all mod_sources before audio processing.
     * Replaces scattered dynamic_cast<AdsrEnvelopeProcessor*> gate_on() calls.
     * Default: no-op. Override in processors that respond to note events
     * (AdsrEnvelopeProcessor, GateDelayProcessor).
     */
    virtual void on_note_on(double /*frequency*/) {}

    /**
     * @brief Notification that a note-off event has occurred.
     *
     * Called by Voice::note_off() for all mod_sources.
     * Default: no-op. Override in processors that respond to note events.
     */
    virtual void on_note_off() {}

    /**
     * @brief Declared input port type for bake() chain-level validation (Phase 14).
     * Default: PORT_AUDIO. Override for processors that consume a control-rate signal.
     *
     * For named-port validation (Phase 15) use declare_port() instead.
     */
    virtual PortType input_port_type() const { return PortType::PORT_AUDIO; }

    // -----------------------------------------------------------------------
    // Phase 15: Named port and parameter declaration
    // -----------------------------------------------------------------------

    /**
     * @brief All ports declared by this processor instance.
     * Populated by declare_port() calls in subclass constructors.
     */
    const std::vector<PortDescriptor>& ports() const { return ports_; }

    /**
     * @brief All parameters declared by this processor instance.
     * Populated by declare_parameter() calls in subclass constructors.
     */
    const std::vector<ParameterDescriptor>& parameters() const { return parameters_; }

    /**
     * @brief Find a declared port by name. Returns nullptr if not found.
     */
    const PortDescriptor* find_port(std::string_view name) const {
        for (const auto& p : ports_)
            if (p.name == name) return &p;
        return nullptr;
    }

    /**
     * @brief Get performance metrics.
     * 
     * Returns zero values when AUDIO_ENABLE_PROFILING is not defined.
     */
    PerformanceMetrics get_metrics() const {
#if AUDIO_ENABLE_PROFILING
        return PerformanceMetrics{
            profiler_.elapsed(),
            profiler_.max_execution_time(),
            profiler_.total_blocks_processed()
        };
#else
        return PerformanceMetrics{};
#endif
    }

protected:
    /**
     * @brief Protected list of input sources this processor pulls from.
     */
    std::vector<InputSource*> inputs_;

    /**
     * @brief Register a named port on this processor (call in subclass constructor).
     *
     * Example:
     *   declare_port({"audio_out",    PORT_AUDIO,   OUT});
     *   declare_port({"envelope_out", PORT_CONTROL, OUT, true}); // unipolar
     *   declare_port({"gain_cv",      PORT_CONTROL, IN,  true});
     */
    void declare_port(PortDescriptor pd) {
        ports_.push_back(std::move(pd));
    }

    /**
     * @brief Register a named parameter on this processor (call in subclass constructor).
     *
     * Example:
     *   declare_parameter({"cutoff", "Cutoff Frequency", 20.0f, 20000.0f, 1000.0f, true});
     */
    void declare_parameter(ParameterDescriptor pd) {
        parameters_.push_back(std::move(pd));
    }

private:
    std::string tag_;
    std::vector<PortDescriptor>      ports_;
    std::vector<ParameterDescriptor> parameters_;

    /**
     * @brief Pure virtual method for subclasses to implement actual processing (Mono).
     * 
     * @param output Output buffer to fill
     * @param voice_context Optional voice context
     */
    virtual void do_pull(std::span<float> output, const VoiceContext* voice_context = nullptr) = 0;

    /**
     * @brief Virtual method for subclasses to implement actual processing (Stereo).
     * 
     * Default implementation calls mono do_pull and copies L to R.
     * 
     * @param output Stereo buffer to fill
     * @param voice_context Optional voice context
     */
    virtual void do_pull(AudioBuffer& output, const VoiceContext* voice_context = nullptr) {
        do_pull(output.left, voice_context);
        std::copy(output.left.begin(), output.left.end(), output.right.begin());
    }

#if AUDIO_ENABLE_PROFILING
    /**
     * @brief Performance profiler instance.
     */
    PerformanceProfiler profiler_;
#endif
};

} // namespace audio

#endif // PROCESSOR_HPP
