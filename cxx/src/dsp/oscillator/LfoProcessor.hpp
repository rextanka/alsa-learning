/**
 * @file LfoProcessor.hpp
 * @brief Low Frequency Oscillator with block-rate calculation and smoothing.
 */

#ifndef LFO_PROCESSOR_HPP
#define LFO_PROCESSOR_HPP

#include "../Processor.hpp"
#include "../SmoothedParam.hpp"
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

    static constexpr float kRampSeconds = 0.010f; // 10 ms

    explicit LfoProcessor(int sample_rate)
        : sample_rate_(sample_rate)
        , ramp_samples_(static_cast<int>(static_cast<float>(sample_rate) * kRampSeconds))
        , phase_(0.0)
        , waveform_(Waveform::Sine)
    {
        // Phase 15: named port declarations
        declare_port({"rate_cv",     PORT_CONTROL, PortDirection::IN,  true});  // unipolar, scales frequency
        declare_port({"reset",       PORT_CONTROL, PortDirection::IN,  true});  // lifecycle-style trigger
        declare_port({"control_out", PORT_CONTROL, PortDirection::OUT, false}); // bipolar [-1,1]

        declare_parameter({"rate",      "LFO Rate",      0.01f, 20.0f,  1.0f, true});
        declare_parameter({"intensity", "LFO Intensity", 0.0f,   1.0f,  1.0f});
        declare_parameter({"waveform",  "LFO Waveform",  0.0f,   3.0f,  0.0f});
        declare_parameter({"delay",     "LFO Delay",     0.0f,  10.0f,  0.0f, true});
    }

    void set_frequency(double freq) override {
        rate_.set_target(static_cast<float>(freq), ramp_samples_);
    }

    void set_intensity(float intensity) {
        intensity_.set_target(intensity, ramp_samples_);
    }

    void set_waveform(Waveform wave) {
        waveform_ = wave;
    }

    bool apply_parameter(const std::string& name, float value) override {
        if (name == "rate") {
            rate_.set_target(static_cast<float>(std::max(0.01f, value)), ramp_samples_);
            return true;
        }
        if (name == "intensity") {
            intensity_.set_target(std::clamp(value, 0.0f, 1.0f), ramp_samples_);
            return true;
        }
        if (name == "waveform") {
            // snap — discrete selector
            int w = static_cast<int>(value);
            if (w >= 0 && w <= 3) {
                waveform_ = static_cast<Waveform>(w);
                return true;
            }
            return false;
        }
        if (name == "delay") {
            delay_time_.set_target(std::max(0.0f, value), ramp_samples_);
            delay_samples_remaining_ = static_cast<size_t>(delay_time_.get() * static_cast<float>(sample_rate_));
            return true;
        }
        return false;
    }

    void reset() override {
        phase_ = 0.0;
        // Snap intensity to target immediately (matches pre-Phase-21 behavior where
        // reset() set smoothed_intensity_ = intensity_ to bypass the ramp).
        intensity_.snap();
        rate_.snap();
        delay_time_.snap();
        // Reset delay countdown on note-on
        delay_samples_remaining_ = static_cast<size_t>(delay_time_.get() * static_cast<float>(sample_rate_));
    }

    PortType output_port_type() const override { return PortType::PORT_CONTROL; }

protected:
    void do_pull(std::span<float> output, const VoiceContext* /* context */ = nullptr) override {
        const int n = static_cast<int>(output.size());
        rate_.advance(n);
        intensity_.advance(n);
        delay_time_.advance(n);

        // During delay window, output zero and do not advance phase.
        if (delay_samples_remaining_ > 0) {
            const size_t consumed = std::min(delay_samples_remaining_, output.size());
            delay_samples_remaining_ -= consumed;
            std::fill(output.begin(), output.end(), 0.0f);
            return;
        }

        // Block-rate update
        const double frequency = static_cast<double>(rate_.get());
        const double phase_inc = frequency / static_cast<double>(sample_rate_);
        const size_t frames = output.size();

        // Calculate LFO value once per block
        float lfo_val = calculate_waveform();
        float final_val = lfo_val * intensity_.get();

        for (auto& sample : output) {
            sample = final_val;
        }

        // Advance phase for next block
        phase_ = std::fmod(phase_ + phase_inc * static_cast<double>(frames), 1.0);
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
                return static_cast<float>(std::sin(2.0 * M_PI * phase_));
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

    int sample_rate_;
    int ramp_samples_;
    double phase_;
    Waveform waveform_;

    SmoothedParam rate_{1.0f};
    SmoothedParam intensity_{1.0f};
    SmoothedParam delay_time_{0.0f};

    size_t delay_samples_remaining_ = 0; // countdown in samples
};

} // namespace audio

#endif // LFO_PROCESSOR_HPP
