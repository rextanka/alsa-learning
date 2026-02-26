/**
 * @file PulseOscillatorProcessor.hpp
 * @brief Pulse wave oscillator with anti-aliasing.
 */

#ifndef PULSE_OSCILLATOR_PROCESSOR_HPP
#define PULSE_OSCILLATOR_PROCESSOR_HPP

#include "SquareOscillatorProcessor.hpp"

namespace audio {

/**
 * @brief Pulse wave oscillator with variable pulse width and PolyBLEP anti-aliasing.
 */
class PulseOscillatorProcessor : public SquareOscillatorProcessor {
public:
    explicit PulseOscillatorProcessor(int sample_rate)
        : SquareOscillatorProcessor(sample_rate)
        , base_pulse_width_(0.5)
    {
    }

    void set_pulse_width(float width) {
        base_pulse_width_ = std::clamp(static_cast<double>(width), 0.01, 0.99);
    }

    void set_pulse_width_modulation(float delta) {
        pwm_delta_ = delta;
    }

    /**
     * @brief Get current phase for sub-oscillator tracking.
     */
    double get_phase() const { return phase_; }

protected:
    double base_pulse_width_;
    double pwm_delta_ = 0.0;

    double generate_sample() override {
        // Update phase accumulator
        phase_ += current_freq_ / sample_rate_;
        
        // Wrap phase to [0.0, 1.0)
        if (phase_ >= 1.0) phase_ -= 1.0;
        if (phase_ < 0.0) phase_ += 1.0;

        // Generate pulse wave with PolyBLEP correction
        const double dt = current_freq_ / sample_rate_;
        double current_pw = std::clamp(base_pulse_width_ + pwm_delta_, 0.01, 0.99);
        double naive = (phase_ < current_pw) ? 0.5 : -0.5;
        
        // Apply correction at transitions (0.0 and current_pw)
        naive += poly_blep(phase_, dt);
        naive -= poly_blep(std::fmod(phase_ + (1.0 - current_pw), 1.0), dt);
        
        return naive;
    }
};

} // namespace audio

#endif // PULSE_OSCILLATOR_PROCESSOR_HPP
