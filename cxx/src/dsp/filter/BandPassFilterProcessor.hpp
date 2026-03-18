/**
 * @file BandPassFilterProcessor.hpp
 * @brief 2-pole biquad band-pass filter (constant 0-dB peak).
 *
 * RT-SAFE chain node: PORT_AUDIO in → PORT_AUDIO out.
 * Type name: BAND_PASS_FILTER
 *
 * Passes a band of frequencies between a low and high edge, attenuating
 * frequencies outside that band. Internally implemented as a single biquad BPF
 * (Audio EQ Cookbook "BPF (constant 0 dB peak gain)"):
 *   b0 =  sin(ω)/2 = Q·α
 *   b1 =  0
 *   b2 = -sin(ω)/2
 *   a0 =  1 + α
 *   a1 = -2·cos(ω)
 *   a2 =  1 - α
 *
 * Parameters center_freq and bandwidth map to fc and Q:
 *   Q = fc / bandwidth (bandwidth in Hz between −3 dB points)
 * Or use the resonance parameter directly (Q = 0.5 + resonance * 9.5).
 *
 * Roland §5-6, Fig 5-6c: "LPF and HPF in series".
 */

#ifndef BAND_PASS_FILTER_PROCESSOR_HPP
#define BAND_PASS_FILTER_PROCESSOR_HPP

#include "../Processor.hpp"
#include "../SmoothedParam.hpp"
#include <algorithm>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace audio {

class BandPassFilterProcessor : public Processor {
public:
    static constexpr float kRampSeconds = 0.010f; // 10 ms

    explicit BandPassFilterProcessor(int sample_rate)
        : sample_rate_(sample_rate)
        , ramp_samples_(static_cast<int>(static_cast<float>(sample_rate) * kRampSeconds))
    {
        declare_port({"audio_in",  PORT_AUDIO,   PortDirection::IN});
        declare_port({"audio_out", PORT_AUDIO,   PortDirection::OUT});
        declare_port({"cutoff_cv", PORT_CONTROL, PortDirection::IN, false}); // bipolar 1V/oct

        declare_parameter({"center_freq", "Center Frequency", 20.0f, 20000.0f, 1000.0f, true});
        declare_parameter({"resonance",   "Resonance (Q)",    0.0f,      1.0f,    0.25f});

        update_coefficients();
    }

    void reset() override { x1_ = x2_ = y1_ = y2_ = 0.0f; }

    bool apply_parameter(const std::string& name, float value) override {
        if (name == "center_freq" || name == "cutoff") {
            center_freq_.set_target(std::clamp(value, 20.0f, static_cast<float>(sample_rate_) * 0.45f), ramp_samples_);
            update_coefficients(); return true;
        }
        if (name == "resonance") {
            q_.set_target(0.5f + std::clamp(value, 0.0f, 1.0f) * 9.5f, ramp_samples_);
            update_coefficients(); return true;
        }
        if (name == "cutoff_cv") {
            cutoff_cv_ = value; update_coefficients(); return true;
        }
        return false;
    }

    PortType output_port_type() const override { return PortType::PORT_AUDIO; }

protected:
    void do_pull(std::span<float> output,
                 const VoiceContext* /*ctx*/ = nullptr) override {
        const int n = static_cast<int>(output.size());
        if (center_freq_.is_ramping() || q_.is_ramping()) {
            center_freq_.advance(n);
            q_.advance(n);
            update_coefficients();
        } else {
            center_freq_.advance(n);
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
        const float fc    = std::max(20.0f, center_freq_.get()
                              * std::pow(2.0f, cutoff_cv_));
        const float w0    = 2.0f * static_cast<float>(M_PI) * fc
                              / static_cast<float>(sample_rate_);
        const float sin_w = std::sin(w0);
        const float cos_w = std::cos(w0);
        const float alpha  = sin_w / (2.0f * q_.get());
        const float a0_inv = 1.0f / (1.0f + alpha);

        b0_ =  alpha        * a0_inv;  // sin(ω)/2 = Q*α
        b1_ =  0.0f;
        b2_ = -alpha        * a0_inv;
        a1_ = (-2.0f*cos_w) * a0_inv;
        a2_ = ( 1.0f-alpha) * a0_inv;
    }

    int   sample_rate_;
    int   ramp_samples_;
    SmoothedParam center_freq_{1000.0f};
    float cutoff_cv_   = 0.0f;
    SmoothedParam q_{2.875f}; // resonance=0.25 default

    float b0_ = 0.0f, b1_ = 0.0f, b2_ = 0.0f;
    float a1_ = 0.0f, a2_ = 0.0f;

    float x1_ = 0.0f, x2_ = 0.0f;
    float y1_ = 0.0f, y2_ = 0.0f;
};

} // namespace audio

#endif // BAND_PASS_FILTER_PROCESSOR_HPP
