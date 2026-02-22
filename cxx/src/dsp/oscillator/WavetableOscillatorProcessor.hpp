/**
 * @file WavetableOscillatorProcessor.hpp
 * @brief Wavetable oscillator with linear interpolation and runtime wave type switching.
 *
 * This file follows the project rules defined in .cursorrules:
 * - Pull Model: Oscillators are source nodes, generate directly.
 * - Modern C++: Target C++20/23 for all new code.
 *
 * Pre-calculates one cycle into a table; reading from memory is faster than
 * computing transcendental functions per sample. Supports runtime wave type switching.
 */

#ifndef AUDIO_OSCILLATOR_WAVETABLE_OSCILLATOR_PROCESSOR_HPP
#define AUDIO_OSCILLATOR_WAVETABLE_OSCILLATOR_PROCESSOR_HPP

#include <cmath>
#include <vector>
#include <span>
#include "../Processor.hpp"
#include "VoiceContext.hpp"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace audio {

/**
 * @brief Wave type enumeration for wavetable oscillator.
 */
enum class WaveType {
    Sine,
    Saw,
    Square,
    Triangle
};

/**
 * @brief Wavetable oscillator with linear interpolation.
 *
 * Inherits from Processor directly. Uses a phase accumulator and linear interpolation
 * between table entries for smooth pitch shifting at any frequency.
 */
class WavetableOscillatorProcessor : public Processor {
public:
    /**
     * @param sample_rate Sample rate in Hz
     * @param table_size Number of samples per cycle (default 2048)
     * @param wave_type Initial waveform type (default Sine)
     */
    explicit WavetableOscillatorProcessor(double sample_rate, int table_size = 2048, WaveType wave_type = WaveType::Sine)
        : sample_rate_(sample_rate)
        , table_size_(table_size > 0 ? table_size : 2048)
        , current_freq_(0.0)
        , target_freq_(0.0)
        , freq_step_(0.0)
        , transitioning_(false)
        , phase_(0.0)
        , phase_increment_(0.0)
    {
        table_.resize(static_cast<size_t>(table_size_));
        setWaveType(wave_type);
    }

    /**
     * @brief Set frequency in Hz (instant change).
     */
    void setFrequency(double freq) {
        current_freq_ = freq;
        target_freq_ = freq;
        transitioning_ = false;
        update_phase_increment();
    }

    /**
     * @brief Set frequency with glide/sweep over duration.
     * 
     * @param target_freq Target frequency in Hz
     * @param duration_seconds Duration of sweep in seconds
     */
    void setFrequencyGlide(double target_freq, double duration_seconds) {
        target_freq_ = target_freq;
        
        if (duration_seconds > 0.0) {
            const long total_samples = static_cast<long>(duration_seconds * sample_rate_);
            freq_step_ = (target_freq - current_freq_) / total_samples;
            transitioning_ = true;
        } else {
            // Instant jump if duration is 0
            setFrequency(target_freq);
        }
    }

    /**
     * @brief Get current frequency.
     */
    double getFrequency() const {
        return current_freq_;
    }

    /**
     * @brief Change the waveform by repopulating the table.
     * 
     * Formulas (where i is current index, L is table_size_):
     * - Sine: sin(2 * PI * i / L)
     * - Saw: 1.0 - (2.0 * i / L)
     * - Square: (i < L / 2) ? 1.0 : -1.0
     * - Triangle: value = -1.0 + (2.0 * i / L); output = 2.0 * (fabs(value) - 0.5)
     */
    void setWaveType(WaveType type) {
        const double L = static_cast<double>(table_size_);
        
        for (int i = 0; i < table_size_; ++i) {
            const double idx = static_cast<double>(i);
            double value = 0.0;
            
            switch (type) {
                case WaveType::Sine:
                    value = std::sin(2.0 * M_PI * idx / L);
                    break;
                    
                case WaveType::Saw:
                    value = 1.0 - (2.0 * idx / L);
                    break;
                    
                case WaveType::Square:
                    value = (idx < L / 2.0) ? 1.0 : -1.0;
                    break;
                    
                case WaveType::Triangle: {
                    double temp = -1.0 + (2.0 * idx / L);
                    value = 2.0 * (std::fabs(temp) - 0.5);
                    break;
                }
            }
            
            table_[static_cast<size_t>(i)] = value;
        }
    }

    void reset() override {
        current_freq_ = 0.0;
        target_freq_ = 0.0;
        freq_step_ = 0.0;
        transitioning_ = false;
        phase_ = 0.0;
        update_phase_increment();
    }

protected:
    void do_pull(std::span<float> output, const VoiceContext* /* voice_context */ = nullptr) override {
        for (auto& sample : output) {
            // Update frequency ramp if transitioning
            update_frequency_ramp();
            
            // Linear interpolation for smooth pitch shifting
            const double index = phase_;
            const int i0 = static_cast<int>(index) % table_size_;
            const int i1 = (i0 + 1) % table_size_;
            const double fraction = index - std::floor(index);
            
            const double a = table_[static_cast<size_t>(i0)];
            const double b = table_[static_cast<size_t>(i1)];
            sample = static_cast<float>(a + fraction * (b - a));
            
            // Advance phase
            phase_ += phase_increment_;
            if (phase_ >= static_cast<double>(table_size_)) {
                phase_ -= static_cast<double>(table_size_);
            } else if (phase_ < 0.0) {
                phase_ += static_cast<double>(table_size_);
            }
        }
    }

    void do_pull(AudioBuffer& output, const VoiceContext* voice_context = nullptr) override {
        for (size_t i = 0; i < output.frames(); ++i) {
            update_frequency_ramp();
            
            const double index = phase_;
            const int i0 = static_cast<int>(index) % table_size_;
            const int i1 = (i0 + 1) % table_size_;
            const double fraction = index - std::floor(index);
            
            const double a = table_[static_cast<size_t>(i0)];
            const double b = table_[static_cast<size_t>(i1)];
            float sample = static_cast<float>(a + fraction * (b - a));
            
            output.left[i] = sample;
            output.right[i] = sample;

            phase_ += phase_increment_;
            if (phase_ >= static_cast<double>(table_size_)) {
                phase_ -= static_cast<double>(table_size_);
            } else if (phase_ < 0.0) {
                phase_ += static_cast<double>(table_size_);
            }
        }
    }

private:
    void update_frequency_ramp() {
        if (transitioning_) {
            current_freq_ += freq_step_;
            
            // Stop at target frequency to prevent overshoot
            if ((freq_step_ > 0.0 && current_freq_ >= target_freq_) ||
                (freq_step_ < 0.0 && current_freq_ <= target_freq_)) {
                current_freq_ = target_freq_;
                transitioning_ = false;
            }
            
            // Update phase increment for the new frequency
            update_phase_increment();
        }
    }

    void update_phase_increment() {
        if (sample_rate_ > 0.0 && table_size_ > 0) {
            phase_increment_ = (current_freq_ * static_cast<double>(table_size_)) / sample_rate_;
        } else {
            phase_increment_ = 0.0;
        }
    }

    std::vector<double> table_;
    double sample_rate_;
    int table_size_;
    double current_freq_;
    double target_freq_;
    double freq_step_;
    bool transitioning_;
    double phase_;
    double phase_increment_;
};

} // namespace audio

#endif // AUDIO_OSCILLATOR_WAVETABLE_OSCILLATOR_PROCESSOR_HPP
