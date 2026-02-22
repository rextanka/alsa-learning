/**
 * @file FilterProcessor.hpp
 * @brief Base class for audio filters.
 */

#ifndef AUDIO_FILTER_PROCESSOR_HPP
#define AUDIO_FILTER_PROCESSOR_HPP

#include "../Processor.hpp"

namespace audio {

/**
 * @brief Abstract base class for filter processors.
 */
class FilterProcessor : public Processor {
public:
    virtual ~FilterProcessor() = default;

    /**
     * @brief Set the cutoff frequency.
     * 
     * @param frequency Cutoff frequency in Hz.
     */
    virtual void set_cutoff(float frequency) = 0;

    /**
     * @brief Set the resonance.
     * 
     * @param resonance Resonance value (typically 0.0 to 1.0 or higher).
     */
    virtual void set_resonance(float resonance) = 0;
};

} // namespace audio

#endif // AUDIO_FILTER_PROCESSOR_HPP
