/**
 * @file SineOscillatorProcessor.hpp
 * @brief Sine wave oscillator using rotor-based generation.
 * 
 * This file follows the project rules defined in .cursorrules:
 * - Modern C++: Target C++20/23 for all new code.
 * - Preserves existing C algorithm: rotor-based sine with periodic normalization.
 */

#ifndef SINE_OSCILLATOR_PROCESSOR_HPP
#define SINE_OSCILLATOR_PROCESSOR_HPP

#include <cmath>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#include "OscillatorProcessor.hpp"

namespace audio {

/**
 * @brief Sine wave oscillator using rotor-based generation.
 * 
 * Uses a complex rotor (x, y) that rotates around the unit circle.
 * Provides high-quality sine waves with periodic normalization to prevent
 * floating-point drift. Preserves the algorithm from the original C implementation.
 */
class SineOscillatorProcessor : public OscillatorProcessor {
public:
    explicit SineOscillatorProcessor(int sample_rate)
        : OscillatorProcessor(sample_rate)
        , x_(1.0)
        , y_(0.0)
        , cos_step_(1.0)
        , sin_step_(0.0)
        , sample_count_(0)
    {
        if (current_freq_ > 0.0) {
            update_rotation_steps();
        }
    }

    void reset() override {
        OscillatorProcessor::reset();
        reset_oscillator_state();
    }

protected:
    static constexpr int NORMALIZE_INTERVAL = 1024;

    double x_;           // Rotor x component
    double y_;           // Rotor y component (output: sin = y)
    double cos_step_;    // Pre-calculated cos(angle_per_sample)
    double sin_step_;    // Pre-calculated sin(angle_per_sample)
    int sample_count_;   // Counter for normalization intervals

    void reset_oscillator_state() override {
        x_ = 1.0;
        y_ = 0.0;
        sample_count_ = 0;
        if (current_freq_ > 0.0) {
            update_rotation_steps();
        }
    }

    void update_rotation_steps() override {
        if (current_freq_ > 0.0 && sample_rate_ > 0) {
            const double angle_per_sample = 2.0 * M_PI * current_freq_ / sample_rate_;
            cos_step_ = std::cos(angle_per_sample);
            sin_step_ = std::sin(angle_per_sample);
        }
    }

    double generate_sample() override {
        // Update rotor (rotation around unit circle)
        const double next_x = x_ * cos_step_ - y_ * sin_step_;
        const double next_y = x_ * sin_step_ + y_ * cos_step_;
        x_ = next_x;
        y_ = next_y;

        // Periodic normalization to prevent floating-point drift
        if (++sample_count_ >= NORMALIZE_INTERVAL) {
            const double magnitude = std::sqrt(x_ * x_ + y_ * y_);
            if (magnitude > 0.0) {
                x_ /= magnitude;
                y_ /= magnitude;
            }
            sample_count_ = 0;
        }

        // Return y component (sine value)
        return y_;
    }
};

} // namespace audio

#endif // SINE_OSCILLATOR_PROCESSOR_HPP
