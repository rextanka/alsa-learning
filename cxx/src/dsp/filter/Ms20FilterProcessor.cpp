/**
 * @file Ms20FilterProcessor.cpp
 * @brief MS-20 dual SVF constructor, process_sample, and self-registration.
 */
#include "Ms20FilterProcessor.hpp"
#include "../../core/ModuleRegistry.hpp"
#include <cmath>

namespace audio {

Ms20FilterProcessor::Ms20FilterProcessor(int sample_rate)
    : VcfBase(sample_rate)
{
    hp_lp_ = hp_bp_ = lp_lp_ = lp_bp_ = 0.0f;

    declare_parameter({"cutoff_hp", "HP Cutoff (Hz)", 20.0f, 2000.0f, 80.0f, true});

    update_cutoff_coefficient(base_cutoff_.get());
    update_hp(cutoff_hp_.get());
    update_q();
}

bool Ms20FilterProcessor::apply_parameter(const std::string& name, float value) {
    if (name == "cutoff_hp") {
        cutoff_hp_.set_target(std::clamp(value, 20.0f, static_cast<float>(sample_rate_) * 0.45f), ramp_samples_);
        update_hp(cutoff_hp_.get());
        return true;
    }
    return VcfBase::apply_parameter(name, value);
}

float Ms20FilterProcessor::svf_f(float cutoff) const {
    float f = 2.0f * std::sin(static_cast<float>(M_PI) * cutoff
                              / static_cast<float>(sample_rate_));
    return std::clamp(f, 0.0001f, 1.99f);
}

void Ms20FilterProcessor::process_sample(float& sample) {
    // Stage 1: HP section (removes mud below cutoff_hp)
    float hp_out    = sample - q_ * hp_bp_ - hp_lp_;
    float hp_bp_new = f_hp_ * hp_out + hp_bp_;
    float hp_lp_new = f_hp_ * hp_bp_new + hp_lp_;
    hp_bp_ = hp_bp_new;
    hp_lp_ = hp_lp_new;

    // Stage 2: LP section (HP output feeds LP input)
    float lp_hp     = hp_out - q_ * lp_bp_ - lp_lp_;
    float lp_bp_new = f_lp_ * lp_hp + lp_bp_;
    float lp_lp_new = f_lp_ * lp_bp_new + lp_lp_;
    lp_bp_ = lp_bp_new;
    lp_lp_ = lp_lp_new;

    sample = lp_lp_;
}

namespace {
[[maybe_unused]] const bool kRegistered = ModuleRegistry::instance().register_module(
    "MS20_FILTER",
    "MS-style dual 2-pole HP+LP SVF (12 dB/oct each) — aggressive, gritty, screaming",
    "Self-oscillates aggressively at resonance > 0.8. "
    "The HP+LP dual topology gives a mid-scoop characteristic distinct from ladder filters. "
    "Excellent for aggressive leads and industrial percussion.",
    [](int sr) { return std::make_unique<Ms20FilterProcessor>(sr); }
);
} // namespace

} // namespace audio
