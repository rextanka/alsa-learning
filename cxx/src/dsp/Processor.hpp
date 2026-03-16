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
 * @brief Port type classification for typed signal routing (Phase 14).
 *
 * PORT_AUDIO — carries audio-rate samples in [-1, 1].
 * PORT_CONTROL — carries control-rate signals, typically [0, 1] (e.g. envelope levels).
 * Both types run at the same sample rate; the distinction is semantic and is validated
 * at bake() time to prevent misrouted connections.
 */
enum class PortType {
    PORT_AUDIO,
    PORT_CONTROL
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
     * @brief Node tag for signal chain discovery (Phase 14).
     *
     * Tags are assigned by VoiceFactory (e.g. "VCO", "VCF", "ENV", "VCA").
     * Used by Voice::find_by_tag() to locate nodes for parameter routing.
     */
    void set_tag(std::string_view tag) { tag_ = tag; }
    std::string_view tag() const { return tag_; }

    /**
     * @brief Declared output port type for connection validation at bake() time.
     */
    virtual PortType output_port_type() const { return PortType::PORT_AUDIO; }

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

private:
    std::string tag_;

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
