/**
 * @file LfoProcessor.hpp
 * @brief Low Frequency Oscillator with block-rate calculation and smoothing.
 */

#ifndef LFO_PROCESSOR_HPP
#define LFO_PROCESSOR_HPP

#include "../Processor.hpp"
#include "../SmoothedParam.hpp"
#include "../TempoSync.hpp"
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

    void inject_cv(std::string_view port, std::span<const float> data) override {
        if (port == "rate_cv")  rate_cv_in_  = data.empty() ? 0.0f : data[0];
        if (port == "reset")    reset_cv_in_ = data.empty() ? 0.0f : data[0];
    }

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

    // Tempo-sync (Phase 27D) — off by default, fully backward-compatible.
    bool sync_     = false;
    int  division_ = 2; ///< Index into kDivisionMultipliers; 2 = "quarter"

    // CV inputs (rate_cv / reset ports).
    float rate_cv_in_  = 0.0f; ///< Additive Hz modulation of rate each block.
    float reset_cv_in_ = 0.0f; ///< Phase reset on positive edge (prev≤0 → now>0).
    float prev_reset_  = 0.0f; ///< Previous reset_cv value for edge detection.
};

} // namespace audio

#endif // LFO_PROCESSOR_HPP
