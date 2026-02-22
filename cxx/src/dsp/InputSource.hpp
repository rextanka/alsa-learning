/**
 * @file InputSource.hpp
 * @brief Interface for pull model input sources.
 * 
 * This file follows the project rules defined in .cursorrules:
 * - Modern C++: Target C++20/23 for all new code.
 * - Pull Model: Output pulls from graph, processors pull from inputs.
 */

#ifndef INPUT_SOURCE_HPP
#define INPUT_SOURCE_HPP

#include <span>
#include "VoiceContext.hpp"

namespace audio {

/**
 * @brief Interface for sources that can be pulled from (Pull Model).
 * 
 * In the pull model architecture, data flows backward: output pulls from processors,
 * processors pull from their inputs. This interface allows processors to pull data
 * from any source (another processor, oscillator, etc.).
 */
class InputSource {
public:
    virtual ~InputSource() = default;

    /**
     * @brief Pull data from this input source into output span.
     * 
     * @param output Output buffer to fill (mono, block-based)
     * @param voice_context Optional voice context for parameter querying
     */
    virtual void pull(std::span<float> output, const VoiceContext* voice_context = nullptr) = 0;
};

} // namespace audio

#endif // INPUT_SOURCE_HPP
