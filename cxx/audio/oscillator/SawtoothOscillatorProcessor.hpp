/**
 * @file SawtoothOscillatorProcessor.hpp
 * @brief Sawtooth wave oscillator with PolyBLEP anti-aliasing.
 * 
 * This file follows the project rules defined in .cursorrules:
 * - Modern C++: Target C++20/23 for all new code.
 * - Preserves existing C algorithm: phase accumulator + PolyBLEP.
 */

#ifndef SAWTOOTH_OSCILLATOR_PROCESSOR_HPP
#define SAWTOOTH_OSCILLATOR_PROCESSOR_HPP

#include <cmath>
#include "OscillatorProcessor.hpp"

namespace audio {

/**
 * @brief Sawtooth wave oscillator with PolyBLEP anti-aliasing.
 * 
 * Uses a phase accumulator (0.0 to 1.0) for efficient generation.
 * Applies PolyBLEP correction at the wrap-around (1.0 back to 0.0) to eliminate
 * aliasing artifacts. Preserves the algorithm from the original C implementation.
 */
class SawtoothOscillatorProcessor : public OscillatorProcessor {
public:
    explicit SawtoothOscillatorProcessor(int sample_rate)
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

    /**
     * @brief PolyBLEP smoothing function for anti-aliasing.
     * 
     * @param t Current phase (0 to 1)
     * @param dt Phase increment (freq / sample_rate)
     * @return Correction value
     */
    static double poly_blep(double t, double dt) {
        if (t < dt) {
            t /= dt;
            return t + t - t * t - 1.0;
        } else if (t > 1.0 - dt) {
            t = (t - 1.0) / dt;
            return t * t + t + t + 1.0;
        }
        return 0.0;
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

        // Generate sawtooth wave with PolyBLEP correction
        const double dt = current_freq_ / sample_rate_;
        const double naive = (phase_ * 2.0) - 1.0;
        
        // Apply correction at the wrap-around (1.0 back to 0.0)
        return naive - poly_blep(phase_, dt);
    }
};

} // namespace audio

#endif // SAWTOOTH_OSCILLATOR_PROCESSOR_HPP
