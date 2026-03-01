/**
 * @file OscillatorProcessor.hpp
 * @brief Base class for oscillator processors.
 * 
 * This file follows the project rules defined in .cursorrules:
 * - Separation of Concerns: Core DSP logic separated from hardware/OS audio code.
 * - Modern C++: Target C++20/23 for all new code.
 * - Pull Model: Oscillators are source nodes, generate directly.
 */

#ifndef OSCILLATOR_PROCESSOR_HPP
#define OSCILLATOR_PROCESSOR_HPP

#include <cmath>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#include "../Processor.hpp"
#include "Logger.hpp"

namespace audio {

/**
 * @brief Base class for oscillator processors.
 * 
 * Provides common functionality for frequency management, frequency ramping/glide,
 * and sample rate handling. Subclasses implement waveform-specific generation
 * via the pure virtual generate_sample() method.
 */
class OscillatorProcessor : public Processor {
public:
    explicit OscillatorProcessor(int sample_rate)
        : sample_rate_(sample_rate)
        , current_freq_(0.0)
        , target_freq_(0.0)
        , freq_step_(0.0)
        , pitch_mod_(0.0)
        , transitioning_(false)
    {
    }

    /**
     * @brief Set pitch modulation in octaves.
     * 
     * f_final = f_target * 2^pitch_mod
     */
    void set_pitch_modulation(double octaves) {
        pitch_mod_ = octaves;
        update_rotation_steps();
    }

    /**
     * @brief Set frequency (instant change).
     */
    void set_frequency(double freq) {
        current_freq_ = freq;
        target_freq_ = freq;
        transitioning_ = false;
        update_rotation_steps();
    }

    /**
     * @brief Set frequency with glide/sweep over duration.
     * 
     * @param target_freq Target frequency in Hz
     * @param duration_seconds Duration of sweep in seconds
     */
    void set_frequency_glide(double target_freq, double duration_seconds) {
        target_freq_ = target_freq;
        
        if (duration_seconds > 0.0) {
            const long total_samples = static_cast<long>(duration_seconds * sample_rate_);
            freq_step_ = (target_freq - current_freq_) / total_samples;
            transitioning_ = true;
        } else {
            // Instant jump if duration is 0
            set_frequency(target_freq);
        }
    }

    /**
     * @brief Get current frequency.
     */
    double get_frequency() const {
        return current_freq_;
    }

    /**
     * @brief Reset oscillator state.
     */
    void reset() override {
        // PRESERVE current_freq_ to avoid stalling phase increment at 0.0
        if (current_freq_ == 0.0) {
            current_freq_ = 440.0;
            target_freq_ = 440.0;
        }
        freq_step_ = 0.0;
        transitioning_ = false;
        reset_oscillator_state();
    }

    /**
     * @brief Update sample rate.
     */
    void set_sample_rate(int sample_rate) {
        sample_rate_ = sample_rate;
        update_rotation_steps();
    }

protected:
    int sample_rate_;
    double current_freq_;
    double target_freq_;
    double freq_step_;
    double pitch_mod_;
    bool transitioning_;

    /**
     * @brief Get effective frequency including modulation.
     */
    double get_effective_frequency() const {
        if (pitch_mod_ == 0.0) return current_freq_;
        return current_freq_ * std::pow(2.0, pitch_mod_);
    }

    /**
     * @brief Update frequency ramp if transitioning.
     * 
     * Called once per sample during processing.
     */
    void update_frequency_ramp() {
        bool needs_update = false;
        if (transitioning_) {
            current_freq_ += freq_step_;
            
            // Stop at target frequency to prevent overshoot
            if ((freq_step_ > 0.0 && current_freq_ >= target_freq_) ||
                (freq_step_ < 0.0 && current_freq_ <= target_freq_)) {
                current_freq_ = target_freq_;
                transitioning_ = false;
            }
            needs_update = true;
        }

        if (needs_update) {
            update_rotation_steps();
        }
    }

    /**
     * @brief Pure virtual: each subclass implements waveform generation.
     * 
     * @return Sample value in range [-1.0, 1.0]
     */
    virtual double generate_sample() = 0;

    /**
     * @brief Pure virtual: reset waveform-specific state.
     */
    virtual void reset_oscillator_state() = 0;

    /**
     * @brief Update rotation steps (for sine oscillators).
     * 
     * Default implementation does nothing. Override in sine oscillator.
     */
    virtual void update_rotation_steps() {}

    /**
     * @brief Pull Model: Generate block of samples (Mono).
     * 
     * Oscillators are source nodes - they generate directly without inputs.
     */
    void do_pull(std::span<float> output, const VoiceContext* /* voice_context */ = nullptr) override {
        for (auto& sample : output) {
            update_frequency_ramp();
            sample = static_cast<float>(generate_sample());
        }
    }

    /**
     * @brief Pull Model: Generate block of samples (Stereo).
     */
    void do_pull(AudioBuffer& output, const VoiceContext* voice_context = nullptr) override {
        for (size_t i = 0; i < output.frames(); ++i) {
            update_frequency_ramp();
            float sample = static_cast<float>(generate_sample());
            output.left[i] = sample;
            output.right[i] = sample;
        }
    }
};

} // namespace audio

#endif // OSCILLATOR_PROCESSOR_HPP
