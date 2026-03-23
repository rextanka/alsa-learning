/**
 * @file HighPassFilterProcessor.cpp
 * @brief Biquad HPF do_pull, coefficient update, and self-registration.
 */
#include "HighPassFilterProcessor.hpp"
#include "../../core/ModuleRegistry.hpp"
#include <cmath>

namespace audio {

void HighPassFilterProcessor::do_pull(std::span<float> output, const VoiceContext* /*ctx*/) {
    const int n = static_cast<int>(output.size());
    fm_depth_.advance(n);
    const float fm_depth_val = fm_depth_.get();
    if (base_cutoff_.is_ramping() || q_.is_ramping()) {
        base_cutoff_.advance(n);
        q_.advance(n);
        update_coefficients();
    } else {
        base_cutoff_.advance(n);
        q_.advance(n);
    }
    if (!fm_in_.empty() && fm_depth_val != 0.0f) {
        for (size_t i = 0; i < output.size(); ++i) {
            const float eff_cv = cutoff_cv_ + kybd_cv_ + fm_depth_val * fm_in_[i];
            const float fc = std::max(20.0f, base_cutoff_.get() * std::pow(2.0f, eff_cv));
            update_coefficients_at(fc);
            const float out = b0_ * output[i] + b1_ * x1_ + b2_ * x2_
                            - a1_ * y1_ - a2_ * y2_;
            x2_ = x1_; x1_ = output[i];
            y2_ = y1_; y1_ = out;
            output[i] = out;
        }
        update_coefficients();
        fm_in_ = {};
    } else {
        for (auto& s : output) {
            const float out = b0_ * s + b1_ * x1_ + b2_ * x2_
                            - a1_ * y1_ - a2_ * y2_;
            x2_ = x1_; x1_ = s;
            y2_ = y1_; y1_ = out;
            s = out;
        }
    }
}

void HighPassFilterProcessor::update_coefficients() {
    const float fc = std::max(20.0f, base_cutoff_.get() * std::pow(2.0f, cutoff_cv_ + kybd_cv_));
    update_coefficients_at(fc);
}

void HighPassFilterProcessor::update_coefficients_at(float fc) {
    const float w0     = 2.0f * static_cast<float>(M_PI) * fc / static_cast<float>(sample_rate_);
    const float cos_w  = std::cos(w0);
    const float sin_w  = std::sin(w0);
    const float alpha  = sin_w / (2.0f * q_.get());
    const float a0_inv = 1.0f / (1.0f + alpha);

    b0_ =  (1.0f + cos_w) * 0.5f * a0_inv;
    b1_ = -(1.0f + cos_w)        * a0_inv;
    b2_ =  (1.0f + cos_w) * 0.5f * a0_inv;
    a1_ = (-2.0f * cos_w)        * a0_inv;
    a2_ = ( 1.0f - alpha)        * a0_inv;
}

namespace {
[[maybe_unused]] const bool kRegistered = ModuleRegistry::instance().register_module(
    "HIGH_PASS_FILTER",
    "2-pole biquad high-pass filter — brightens by removing low frequencies",
    "Use at cutoff=200–400 Hz to remove low-end rumble from percussion patches. "
    "Useful in parallel paths to separate a bright high-frequency layer from "
    "a low-frequency body before mixing via AUDIO_MIXER.",
    [](int sr) { return std::make_unique<HighPassFilterProcessor>(sr); }
);
} // namespace

} // namespace audio
