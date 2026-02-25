/**
 * @file LfoProcessor.hpp
 * @brief Low Frequency Oscillator with block-rate calculation and smoothing.
 */

#ifndef LFO_PROCESSOR_HPP
#define LFO_PROCESSOR_HPP

#include "../Processor.hpp"
#include <cmath>

namespace audio {

/**
 * @brief LFO Processor for modulation.
 * 
 * Supports Sine, Triangle, Square, and Saw waveforms.
 * Implementation is optimized for block-rate calculation.
 */
class LfoProcessor : public Processor {
public:
    enum class Waveform {
        Sine,
        Triangle,
        Square,
        Saw
    };

    explicit LfoProcessor(int sample_rate)
        : sample_rate_(sample_rate)
        , phase_(0.0)
        , frequency_(1.0)
        , intensity_(1.0)
        , smoothed_intensity_(1.0)
        , waveform_(Waveform::Sine)
    {
        update_smoothing_coeff(0.01); // 10ms default smoothing
    }

    void set_frequency(double freq) {
        frequency_ = freq;
    }

    void set_intensity(float intensity) {
        intensity_ = intensity;
    }

    void set_waveform(Waveform wave) {
        waveform_ = wave;
    }

    void set_smoothing_time(double seconds) {
        update_smoothing_coeff(seconds);
    }

    void reset() override {
        phase_ = 0.0;
        smoothed_intensity_ = intensity_;
    }

protected:
    void do_pull(std::span<float> output, const VoiceContext* /* context */ = nullptr) override {
        // Block-rate update
        const double phase_inc = frequency_ / sample_rate_;
        const size_t frames = output.size();
        
        // Calculate LFO value once per block
        float lfo_val = calculate_waveform();
        
        // Apply smoothing to intensity
        smoothed_intensity_ = smoothed_intensity_ + smoothing_coeff_ * (intensity_ - smoothed_intensity_);
        
        float final_val = lfo_val * smoothed_intensity_;
        
        for (auto& sample : output) {
            sample = final_val;
        }
        
        // Advance phase for next block
        phase_ = std::fmod(phase_ + phase_inc * frames, 1.0);
    }

    void do_pull(AudioBuffer& output, const VoiceContext* context = nullptr) override {
        do_pull(output.left, context);
        // Copy to right for mono-to-stereo modulation consistency
        std::copy(output.left.begin(), output.left.end(), output.right.begin());
    }

private:
    float calculate_waveform() const {
        switch (waveform_) {
            case Waveform::Sine:
                return std::sin(2.0 * M_PI * phase_);
            case Waveform::Triangle:
                return 2.0f * std::abs(2.0f * static_cast<float>(phase_) - 1.0f) - 1.0f;
            case Waveform::Square:
                return (phase_ < 0.5) ? 1.0f : -1.0f;
            case Waveform::Saw:
                return 2.0f * static_cast<float>(phase_) - 1.0f;
            default:
                return 0.0f;
        }
    }

    void update_smoothing_coeff(double seconds) {
        if (seconds <= 0.0) {
            smoothing_coeff_ = 1.0f;
        } else {
            // Simple one-pole alpha calculation
            // For block-rate, we use 1.0 - exp(-block_duration / tau)
            // But we can simplify to a fixed alpha per pull() for LFO intensity
            smoothing_coeff_ = static_cast<float>(1.0 - std::exp(-1.0 / (seconds * (sample_rate_ / 512.0)))); // Approx for 512 block size
        }
    }

    int sample_rate_;
    double phase_;
    double frequency_;
    float intensity_;
    float smoothed_intensity_;
    float smoothing_coeff_;
    Waveform waveform_;
};

} // namespace audio

#endif // LFO_PROCESSOR_HPP
