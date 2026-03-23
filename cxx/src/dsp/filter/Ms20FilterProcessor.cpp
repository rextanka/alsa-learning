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
    declare_port({"fm_in", PORT_AUDIO, PortDirection::IN});
    declare_parameter({"fm_depth", "FM Depth", 0.0f, 1.0f, 0.0f});

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
    if (name == "fm_depth") {
        fm_depth_.set_target(std::clamp(value, 0.0f, 1.0f), ramp_samples_);
        return true;
    }
    return VcfBase::apply_parameter(name, value);
}

void Ms20FilterProcessor::inject_audio(std::string_view port_name, std::span<const float> audio) {
    if (port_name == "fm_in") fm_in_ = audio;
}

void Ms20FilterProcessor::do_pull(std::span<float> output, const VoiceContext* ctx) {
    fm_depth_.advance(static_cast<int>(output.size()));
    const float fm_depth_val = fm_depth_.get();
    if (fm_in_.empty() || fm_depth_val == 0.0f) {
        VcfBase::do_pull(output, ctx);
    } else {
        const int n = static_cast<int>(output.size());
        if (base_cutoff_.is_ramping()) base_cutoff_.advance(n);
        if (base_res_.is_ramping())    base_res_.advance(n);

        for (size_t i = 0; i < output.size(); ++i) {
            const float eff_cv = cutoff_cv_ + kybd_cv_ + fm_depth_val * fm_in_[i];
            const float fc = std::max(20.0f, base_cutoff_.get() * std::pow(2.0f, eff_cv));
            update_cutoff_coefficient(fc);
            process_sample(output[i]);
        }
        update_effective_cutoff();
        fm_in_ = {};
    }
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
