/**
 * @file SquareOscillatorProcessor.hpp
 * @brief Square wave oscillator with PolyBLEP anti-aliasing.
 * 
 * This file follows the project rules defined in .cursorrules:
 * - Modern C++: Target C++20/23 for all new code.
 * - Preserves existing C algorithm: phase accumulator + PolyBLEP.
 */

#ifndef SQUARE_OSCILLATOR_PROCESSOR_HPP
#define SQUARE_OSCILLATOR_PROCESSOR_HPP

#include <cmath>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#include "OscillatorProcessor.hpp"

namespace audio {

/**
 * @brief Square wave oscillator with PolyBLEP anti-aliasing.
 * 
 * Uses a phase accumulator (0.0 to 1.0) for efficient generation.
 * Applies PolyBLEP correction at both transitions (0.0 and 0.5) to eliminate
 * aliasing artifacts. Preserves the algorithm from the original C implementation.
 */
class SquareOscillatorProcessor : public OscillatorProcessor {
public:
    explicit SquareOscillatorProcessor(int sample_rate)
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

        // Generate square wave with PolyBLEP correction
        const double dt = current_freq_ / sample_rate_;
        double naive = (phase_ < 0.5) ? 0.5 : -0.5;
        
        // Apply correction at both transitions (0.0 and 0.5)
        naive += poly_blep(phase_, dt);
        naive -= poly_blep(std::fmod(phase_ + 0.5, 1.0), dt);
        
        return naive;
    }
};

} // namespace audio

#endif // SQUARE_OSCILLATOR_PROCESSOR_HPP
