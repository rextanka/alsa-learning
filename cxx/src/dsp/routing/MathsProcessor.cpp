/**
 * @file MathsProcessor.cpp
 * @brief Slew limiter do_pull and self-registration.
 */
#include "MathsProcessor.hpp"
#include "../../core/ModuleRegistry.hpp"
#include <cmath>

namespace audio {

MathsProcessor::MathsProcessor(int sample_rate)
    : sample_rate_(sample_rate)
    , ramp_samples_(static_cast<int>(static_cast<float>(sample_rate) * kRampSeconds))
{
    declare_port({"cv_in",  PORT_CONTROL, PortDirection::IN,  false});
    declare_port({"cv_out", PORT_CONTROL, PortDirection::OUT, false});

    declare_parameter({"rise",  "Rise Time (s)",  0.0f, 10.0f, 0.0f, true});
    declare_parameter({"fall",  "Fall Time (s)",  0.0f, 10.0f, 0.0f, true});
    declare_parameter({"curve", "Curve (0=Lin 1=Exp)", 0.0f, 1.0f, 0.0f});

    update_rates();
}

bool MathsProcessor::apply_parameter(const std::string& name, float value) {
    if (name == "rise")  { rise_time_.set_target(std::max(0.0f, value), ramp_samples_); update_rates(); return true; }
    if (name == "fall")  { fall_time_.set_target(std::max(0.0f, value), ramp_samples_); update_rates(); return true; }
    if (name == "curve") { curve_ = (value >= 0.5f) ? Curve::Exponential : Curve::Linear; return true; }
    return false;
}

void MathsProcessor::do_pull(std::span<float> output, const VoiceContext*) {
    const int n = static_cast<int>(output.size());
    rise_time_.advance(n);
    fall_time_.advance(n);
    if (rise_time_.is_ramping() || fall_time_.is_ramping())
        update_rates();

    for (size_t i = 0; i < output.size(); ++i) {
        float target = injected_.empty() ? 0.0f
                     : (i < injected_.size() ? injected_[i] : injected_.back());
        output[i] = slew(target);
    }
    injected_ = {};
}

float MathsProcessor::slew(float target) {
    const float delta = target - current_;
    if (curve_ == Curve::Exponential) {
        if (delta > 0.0f) current_ += delta * (1.0f - rise_coeff_);
        else              current_ += delta * (1.0f - fall_coeff_);
    } else {
        if (delta > 0.0f) current_ = std::min(current_ + rise_rate_, target);
        else              current_ = std::max(current_ - fall_rate_, target);
    }
    return current_;
}

void MathsProcessor::update_rates() {
    const float sr = static_cast<float>(sample_rate_);
    const float rt = rise_time_.get();
    const float ft = fall_time_.get();
    rise_rate_ = (rt > 0.0f) ? (2.0f / (rt * sr)) : 1e9f;
    fall_rate_ = (ft > 0.0f) ? (2.0f / (ft * sr)) : 1e9f;
    static constexpr float kLog99 = 4.60517f;
    rise_coeff_ = (rt > 0.0f) ? std::exp(-kLog99 / (rt * sr)) : 0.0f;
    fall_coeff_ = (ft > 0.0f) ? std::exp(-kLog99 / (ft * sr)) : 0.0f;
}

namespace {
[[maybe_unused]] const bool kRegistered = ModuleRegistry::instance().register_module(
    "MATHS",
    "Slew limiter / portamento — rate-limits a CV signal for smooth pitch glide",
    "Connect pitch_cv → MATHS:cv_in → VCO:pitch_cv with rise=0.05 for portamento. "
    "curve=1 (exponential) gives organic deceleration into the target pitch. "
    "rise=0.5, fall=0.01 on a gate signal creates a slow-attack punch envelope.",
    [](int sr) { return std::make_unique<MathsProcessor>(sr); }
);
} // namespace

} // namespace audio
