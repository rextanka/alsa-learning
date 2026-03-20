/**
 * @file HighPassFilterProcessor.hpp
 * @brief 2-pole biquad high-pass filter.
 *
 * RT-SAFE chain node: PORT_AUDIO in → PORT_AUDIO out.
 * Type name: HIGH_PASS_FILTER
 *
 * Removes low frequencies to brighten synthesized sounds.
 * Implemented using the Audio EQ Cookbook biquad HPF:
 *   H(z) = (b0 + b1·z⁻¹ + b2·z⁻²) / (1 + a1·z⁻¹ + a2·z⁻²)
 *   ω  = 2π·fc/fs
 *   α  = sin(ω)/(2·Q)
 *   b0 =  (1 + cos(ω))/2
 *   b1 = -(1 + cos(ω))
 *   b2 =  (1 + cos(ω))/2
 *   a0 =   1 + α
 *   a1 =  -2·cos(ω)
 *   a2 =   1 - α
 */

#ifndef HIGH_PASS_FILTER_PROCESSOR_HPP
#define HIGH_PASS_FILTER_PROCESSOR_HPP

#include "../Processor.hpp"
#include "../SmoothedParam.hpp"
#include <algorithm>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace audio {

class HighPassFilterProcessor : public Processor {
public:
    static constexpr float kRampSeconds = 0.010f;

    explicit HighPassFilterProcessor(int sample_rate)
        : sample_rate_(sample_rate)
        , ramp_samples_(static_cast<int>(static_cast<float>(sample_rate) * kRampSeconds))
    {
        declare_port({"audio_in",  PORT_AUDIO,   PortDirection::IN});
        declare_port({"audio_out", PORT_AUDIO,   PortDirection::OUT});
        declare_port({"cutoff_cv", PORT_CONTROL, PortDirection::IN, false});
        declare_port({"kybd_cv",   PORT_CONTROL, PortDirection::IN, false});

        declare_parameter({"cutoff",    "Cutoff Frequency", 20.0f, 20000.0f, 80.0f, true});
        declare_parameter({"resonance", "Resonance",         0.0f,     1.0f,  0.0f});

        update_coefficients();
    }

    void reset() override { x1_ = x2_ = y1_ = y2_ = 0.0f; }

    bool apply_parameter(const std::string& name, float value) override {
        if (name == "cutoff") {
            base_cutoff_.set_target(std::clamp(value, 20.0f, static_cast<float>(sample_rate_) * 0.45f), ramp_samples_);
            update_coefficients(); return true;
        }
        if (name == "resonance") {
            q_.set_target(0.5f + std::clamp(value, 0.0f, 1.0f) * 9.5f, ramp_samples_);
            update_coefficients(); return true;
        }
        if (name == "cutoff_cv") {
            cutoff_cv_ = value; update_coefficients(); return true;
        }
        if (name == "kybd_cv") {
            kybd_cv_ = value; update_coefficients(); return true;
        }
        return false;
    }

    PortType output_port_type() const override { return PortType::PORT_AUDIO; }

protected:
    void do_pull(std::span<float> output, const VoiceContext* ctx = nullptr) override;

private:
    void update_coefficients();

    int   sample_rate_;
    int   ramp_samples_;
    SmoothedParam base_cutoff_{80.0f};
    float cutoff_cv_ = 0.0f;
    float kybd_cv_   = 0.0f;
    SmoothedParam q_{0.707f};

    float b0_ = 1.0f, b1_ = 0.0f, b2_ = 0.0f;
    float a1_ = 0.0f, a2_ = 0.0f;

    float x1_ = 0.0f, x2_ = 0.0f;
    float y1_ = 0.0f, y2_ = 0.0f;
};

} // namespace audio

#endif // HIGH_PASS_FILTER_PROCESSOR_HPP
