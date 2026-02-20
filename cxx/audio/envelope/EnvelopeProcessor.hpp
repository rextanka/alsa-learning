/**
 * @file EnvelopeProcessor.hpp
 * @brief Base class for envelope processors.
 * 
 * This file follows the project rules defined in .cursorrules:
 * - Separation of Concerns: Core DSP logic separated from hardware/OS audio code.
 * - Modern C++: Target C++20/23 for all new code.
 */

#ifndef ENVELOPE_PROCESSOR_HPP
#define ENVELOPE_PROCESSOR_HPP

#include "../Processor.hpp"

namespace audio {

/**
 * @brief Base class for envelope processors.
 * 
 * Envelopes provide control signals (typically 0.0 to 1.0) to modulate
 * parameters like gain or filter cutoff.
 */
class EnvelopeProcessor : public Processor {
public:
    virtual ~EnvelopeProcessor() = default;

    /**
     * @brief Trigger the envelope's "on" stage (e.g., Attack).
     */
    virtual void gate_on() = 0;

    /**
     * @brief Trigger the envelope's "off" stage (e.g., Release).
     */
    virtual void gate_off() = 0;

    /**
     * @brief Check if the envelope is currently active (not in Idle state).
     * 
     * @return true if the envelope is processing, false if it has finished.
     */
    virtual bool is_active() const = 0;
};

} // namespace audio

#endif // ENVELOPE_PROCESSOR_HPP
