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
 *
 * Portamento usage:
 *   voice: VCO pitch is set to target_freq on note_on. Patch MATHS cv_in to
 *   receive a separate pitch-CV source (e.g., from a future KEYBOARD node).
 *   For now, MATHS with its slew on VCO pitch_cv smooths pitch transitions
 *   between successive notes.
 */

#ifndef MATHS_PROCESSOR_HPP
#define MATHS_PROCESSOR_HPP

#include "../Processor.hpp"
#include <algorithm>
#include <cmath>

namespace audio {

class MathsProcessor : public Processor {
public:
    enum class Curve { Linear = 0, Exponential = 1 };

    explicit MathsProcessor(int sample_rate)
        : sample_rate_(sample_rate)
    {
        declare_port({"cv_in",  PORT_CONTROL, PortDirection::IN,  false});
        declare_port({"cv_out", PORT_CONTROL, PortDirection::OUT, false});

        declare_parameter({"rise",  "Rise Time (s)",  0.0f, 10.0f, 0.0f, true});
        declare_parameter({"fall",  "Fall Time (s)",  0.0f, 10.0f, 0.0f, true});
        declare_parameter({"curve", "Curve (0=Lin 1=Exp)", 0.0f, 1.0f, 0.0f});

        update_rates();
    }

    PortType output_port_type() const override { return PortType::PORT_CONTROL; }

    void reset() override {
        current_ = 0.0f;
        injected_ = {};
    }

    bool apply_parameter(const std::string& name, float value) override {
        if (name == "rise")  { rise_time_  = std::max(0.0f, value); update_rates(); return true; }
        if (name == "fall")  { fall_time_  = std::max(0.0f, value); update_rates(); return true; }
        if (name == "curve") { curve_ = (value >= 0.5f) ? Curve::Exponential : Curve::Linear; return true; }
        return false;
    }

    void inject_cv(std::string_view port_name, std::span<const float> cv) override {
        if (port_name == "cv_in") injected_ = cv;
    }

protected:
    void do_pull(std::span<float> output,
                 const VoiceContext* /*ctx*/ = nullptr) override {
        for (size_t i = 0; i < output.size(); ++i) {
            float target = injected_.empty() ? 0.0f
                         : (i < injected_.size() ? injected_[i] : injected_.back());
            output[i] = slew(target);
        }
        injected_ = {};
    }

private:
    float slew(float target) {
        const float delta = target - current_;
        if (curve_ == Curve::Exponential) {
            // One-pole IIR: output tracks target with time constant
            if (delta > 0.0f) current_ += delta * (1.0f - rise_coeff_);
            else              current_ += delta * (1.0f - fall_coeff_);
        } else {
            // Linear: advance at constant rate per sample
            if (delta > 0.0f) current_ = std::min(current_ + rise_rate_, target);
            else              current_ = std::max(current_ - fall_rate_, target);
        }
        return current_;
    }

    void update_rates() {
        const float sr = static_cast<float>(sample_rate_);
        // Linear: full 2.0 range in rise_time_ seconds → rate per sample
        rise_rate_ = (rise_time_ > 0.0f) ? (2.0f / (rise_time_ * sr)) : 1e9f;
        fall_rate_ = (fall_time_ > 0.0f) ? (2.0f / (fall_time_ * sr)) : 1e9f;
        // Exponential: reaches 99% of target in time seconds
        static constexpr float kLog99 = 4.60517f; // log(99)
        rise_coeff_ = (rise_time_ > 0.0f) ? std::exp(-kLog99 / (rise_time_ * sr)) : 0.0f;
        fall_coeff_ = (fall_time_ > 0.0f) ? std::exp(-kLog99 / (fall_time_ * sr)) : 0.0f;
    }

    int   sample_rate_;
    float rise_time_ = 0.0f;
    float fall_time_ = 0.0f;
    Curve curve_     = Curve::Linear;

    float rise_rate_ = 1e9f; // linear delta/sample (instant when rise_time == 0)
    float fall_rate_ = 1e9f;
    float rise_coeff_ = 0.0f; // IIR coefficient (exponential curve)
    float fall_coeff_ = 0.0f;

    float current_ = 0.0f;
    std::span<const float> injected_;
};

} // namespace audio

#endif // MATHS_PROCESSOR_HPP
