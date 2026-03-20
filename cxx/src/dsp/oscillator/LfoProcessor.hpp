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
    enum class Waveform { Sine, Triangle, Square, Saw };

    static constexpr float kRampSeconds = 0.010f;

    explicit LfoProcessor(int sample_rate);

    void set_frequency(double freq) override {
        rate_.set_target(static_cast<float>(freq), ramp_samples_);
    }

    void set_intensity(float intensity) {
        intensity_.set_target(intensity, ramp_samples_);
    }

    void set_waveform(Waveform wave) { waveform_ = wave; }

    bool apply_parameter(const std::string& name, float value) override;

    void reset() override;

    PortType output_port_type() const override { return PortType::PORT_CONTROL; }

protected:
    void do_pull(std::span<float> output, const VoiceContext* ctx = nullptr) override;
    void do_pull(AudioBuffer& output, const VoiceContext* ctx = nullptr) override;

private:
    float calculate_waveform() const;

    int      sample_rate_;
    int      ramp_samples_;
    double   phase_;
    Waveform waveform_;

    SmoothedParam rate_{1.0f};
    SmoothedParam intensity_{1.0f};
    SmoothedParam delay_time_{0.0f};

    size_t delay_samples_remaining_ = 0;
};

} // namespace audio

#endif // LFO_PROCESSOR_HPP
