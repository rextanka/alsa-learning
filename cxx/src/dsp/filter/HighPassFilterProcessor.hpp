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
 *
 * Psychoacoustic rule: even with heavy attenuation of the fundamental,
 * the ear perceives pitch from the harmonic series above it.
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
    static constexpr float kRampSeconds = 0.010f; // 10 ms

    explicit HighPassFilterProcessor(int sample_rate)
        : sample_rate_(sample_rate)
        , ramp_samples_(static_cast<int>(static_cast<float>(sample_rate) * kRampSeconds))
    {
        declare_port({"audio_in",  PORT_AUDIO,   PortDirection::IN});
        declare_port({"audio_out", PORT_AUDIO,   PortDirection::OUT});
        declare_port({"cutoff_cv", PORT_CONTROL, PortDirection::IN, false}); // bipolar 1V/oct
        declare_port({"kybd_cv",   PORT_CONTROL, PortDirection::IN, false}); // bipolar 1V/oct

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
            // Q = 0.5 + res*9.5; smooth the Q value
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
    void do_pull(std::span<float> output,
                 const VoiceContext* /*ctx*/ = nullptr) override {
        const int n = static_cast<int>(output.size());
        if (base_cutoff_.is_ramping() || q_.is_ramping()) {
            base_cutoff_.advance(n);
            q_.advance(n);
            update_coefficients();
        } else {
            base_cutoff_.advance(n);
            q_.advance(n);
        }
        for (auto& s : output) {
            const float out = b0_ * s + b1_ * x1_ + b2_ * x2_
                            - a1_ * y1_ - a2_ * y2_;
            x2_ = x1_; x1_ = s;
            y2_ = y1_; y1_ = out;
            s = out;
        }
    }

private:
    void update_coefficients() {
        const float fc  = std::max(20.0f, base_cutoff_.get()
                            * std::pow(2.0f, cutoff_cv_ + kybd_cv_));
        const float w0  = 2.0f * static_cast<float>(M_PI) * fc
                            / static_cast<float>(sample_rate_);
        const float cos_w = std::cos(w0);
        const float sin_w = std::sin(w0);
        const float alpha  = sin_w / (2.0f * q_.get());
        const float a0_inv = 1.0f / (1.0f + alpha);

        b0_ =  (1.0f + cos_w) * 0.5f * a0_inv;
        b1_ = -(1.0f + cos_w)        * a0_inv;
        b2_ =  (1.0f + cos_w) * 0.5f * a0_inv;
        a1_ = (-2.0f * cos_w)        * a0_inv;
        a2_ = ( 1.0f - alpha)        * a0_inv;
    }

    int   sample_rate_;
    int   ramp_samples_;
    SmoothedParam base_cutoff_{80.0f};
    float cutoff_cv_   = 0.0f;
    float kybd_cv_     = 0.0f;
    SmoothedParam q_{0.707f}; // Butterworth by default (resonance=0)

    // Biquad coefficients
    float b0_ = 1.0f, b1_ = 0.0f, b2_ = 0.0f;
    float a1_ = 0.0f, a2_ = 0.0f;

    // Delay state
    float x1_ = 0.0f, x2_ = 0.0f;
    float y1_ = 0.0f, y2_ = 0.0f;
};

} // namespace audio

#endif // HIGH_PASS_FILTER_PROCESSOR_HPP
