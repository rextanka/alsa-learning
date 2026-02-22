/**
 * @file TriangleOscillatorProcessor.hpp
 * @brief Triangle wave oscillator.
 * 
 * This file follows the project rules defined in .cursorrules:
 * - Modern C++: Target C++20/23 for all new code.
 * - Preserves existing C algorithm: phase accumulator (naive triangle).
 */

#ifndef TRIANGLE_OSCILLATOR_PROCESSOR_HPP
#define TRIANGLE_OSCILLATOR_PROCESSOR_HPP

#include <cmath>
#include "OscillatorProcessor.hpp"

namespace audio {

/**
 * @brief Triangle wave oscillator.
 * 
 * Uses a phase accumulator (0.0 to 1.0) for efficient generation.
 * Triangle waves are already fairly clean (1/f^2 spectrum), so we use the
 * naive version without PolyBLEP. Preserves the algorithm from the original C implementation.
 */
class TriangleOscillatorProcessor : public OscillatorProcessor {
public:
    explicit TriangleOscillatorProcessor(int sample_rate)
        : OscillatorProcessor(sample_rate)
        , phase_(0.0)
    {
    }

    void reset() override {
        OscillatorProcessor::reset();
        reset_oscillator_state();
    }

protected:
    double phase_;  // Phase accumulator (0.0 to 1.0)

    void reset_oscillator_state() override {
        phase_ = 0.0;
    }

    double generate_sample() override {
        // Update phase accumulator
        phase_ += current_freq_ / sample_rate_;
        
        // Wrap phase to [0.0, 1.0)
        if (phase_ >= 1.0) {
            phase_ -= 1.0;
        } else if (phase_ < 0.0) {
            phase_ += 1.0;
        }

        // Generate triangle wave (naive version - already clean)
        double val;
        if (phase_ < 0.5) {
            val = phase_ * 2.0;
        } else {
            val = 2.0 - (phase_ * 2.0);
        }
        
        return (val * 2.0) - 1.0;
    }
};

} // namespace audio

#endif // TRIANGLE_OSCILLATOR_PROCESSOR_HPP
