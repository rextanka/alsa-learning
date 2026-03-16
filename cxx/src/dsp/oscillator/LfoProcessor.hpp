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
        , smoothing_time_(0.01)
        , last_block_size_(0)
        , waveform_(Waveform::Sine)
    {
        smoothing_coeff_ = 1.0f; // will be computed on first do_pull

        // Phase 15: named port declarations
        declare_port({"lfo_out", PORT_CONTROL, PortDirection::OUT, false}); // bipolar [-1,1]

        declare_parameter({"frequency", "LFO Rate",      0.01f, 20.0f, 1.0f, true});
        declare_parameter({"intensity", "LFO Intensity", 0.0f,   1.0f, 1.0f});
        declare_parameter({"waveform",  "LFO Waveform",  0.0f,   3.0f, 0.0f});
    }

    void set_frequency(double freq) override {
        frequency_ = freq;
    }

    void set_intensity(float intensity) {
        intensity_ = intensity;
    }

    void set_waveform(Waveform wave) {
        waveform_ = wave;
    }

    void set_smoothing_time(double seconds) {
        smoothing_time_ = seconds;
        last_block_size_ = 0; // force recompute on next do_pull
    }

    void reset() override {
        phase_ = 0.0;
        smoothed_intensity_ = intensity_;
    }

protected:
    void do_pull(std::span<float> output, const VoiceContext* /* context */ = nullptr) override {
        // Recompute smoothing coefficient if block size has changed (rare after first call).
        if (output.size() != last_block_size_) {
            last_block_size_ = output.size();
            update_smoothing_coeff(smoothing_time_, output.size());
        }

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

    void update_smoothing_coeff(double seconds, size_t block_size) {
        if (seconds <= 0.0 || block_size == 0) {
            smoothing_coeff_ = 1.0f;
        } else {
            // α = 1 - exp(-block_duration / τ) for exact one-pole at block rate
            const double block_duration = static_cast<double>(block_size) / sample_rate_;
            smoothing_coeff_ = static_cast<float>(1.0 - std::exp(-block_duration / seconds));
        }
    }

    int sample_rate_;
    double phase_;
    double frequency_;
    float intensity_;
    float smoothed_intensity_;
    double smoothing_time_;
    size_t last_block_size_;
    float smoothing_coeff_;
    Waveform waveform_;
};

} // namespace audio

#endif // LFO_PROCESSOR_HPP
