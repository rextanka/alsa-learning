/**
 * @file MathsProcessor.hpp
 * @brief Slew limiter / portamento / function generator.
 *
 * Type name: MATHS
 *
 * Limits the rate of change of an input CV signal. The output tracks the input
 * but cannot move faster than rise_rate_ (going up) or fall_rate_ (going down)
 * per sample. This produces portamento (glide) when placed between keyboard pitch
 * CV and VCO pitch_cv, and smooth attack/release curves when driven by a gate.
 *
 * Ports (PORT_CONTROL):
 *   cv_in  IN  bipolar [-1,1]
 *   cv_out OUT bipolar [-1,1]
 *
 * Parameters:
 *   rise  (0.0 – 10.0 s) — time to slew from -1 to +1; default 0.0 (instant)
 *   fall  (0.0 – 10.0 s) — time to slew from +1 to -1; default 0.0 (instant)
 *   curve (enum 0=Linear, 1=Exponential) — slew curve shape
 */

#ifndef MATHS_PROCESSOR_HPP
#define MATHS_PROCESSOR_HPP

#include "../Processor.hpp"
#include "../SmoothedParam.hpp"
#include <algorithm>
#include <cmath>

namespace audio {

class MathsProcessor : public Processor {
public:
    enum class Curve { Linear = 0, Exponential = 1 };

    static constexpr float kRampSeconds = 0.010f;

    explicit MathsProcessor(int sample_rate);

    PortType output_port_type() const override { return PortType::PORT_CONTROL; }

    void reset() override {
        current_  = 0.0f;
        injected_ = {};
    }

    bool apply_parameter(const std::string& name, float value) override;

    void inject_cv(std::string_view port_name, std::span<const float> cv) override {
        if (port_name == "cv_in") injected_ = cv;
    }

protected:
    void do_pull(std::span<float> output, const VoiceContext* ctx = nullptr) override;

private:
    float slew(float target);
    void  update_rates();

    int   sample_rate_;
    int   ramp_samples_;
    SmoothedParam rise_time_{0.0f};
    SmoothedParam fall_time_{0.0f};
    Curve curve_ = Curve::Linear;

    float rise_rate_  = 1e9f;
    float fall_rate_  = 1e9f;
    float rise_coeff_ = 0.0f;
    float fall_coeff_ = 0.0f;

    float current_ = 0.0f;
    std::span<const float> injected_;
};

} // namespace audio

#endif // MATHS_PROCESSOR_HPP
