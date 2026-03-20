/**
 * @file VcfBase.cpp
 * @brief VcfBase constructor, parameter handling, and do_pull implementations.
 */
#include "VcfBase.hpp"

namespace audio {

VcfBase::VcfBase(int sample_rate)
    : sample_rate_(sample_rate)
    , ramp_samples_(static_cast<int>(static_cast<float>(sample_rate) * kRampSeconds))
{
    declare_port({"audio_in",  PORT_AUDIO,   PortDirection::IN});
    declare_port({"audio_out", PORT_AUDIO,   PortDirection::OUT});
    declare_port({"cutoff_cv", PORT_CONTROL, PortDirection::IN, false}); // bipolar 1V/oct
    declare_port({"res_cv",    PORT_CONTROL, PortDirection::IN, true});  // unipolar additive
    declare_port({"kybd_cv",   PORT_CONTROL, PortDirection::IN, false}); // bipolar 1V/oct
    // fm_in declared in LadderVcfBase for MOOG_FILTER and DIODE_FILTER only

    declare_parameter({"cutoff",    "Cutoff Frequency", 20.0f, 20000.0f, 20000.0f, true});
    declare_parameter({"resonance", "Resonance",         0.0f,     1.0f,     0.0f});
}

bool VcfBase::apply_parameter(const std::string& name, float value) {
    if (name == "cutoff") {
        const float clamp = std::clamp(value, 20.0f, static_cast<float>(sample_rate_) * 0.45f);
        base_cutoff_.set_target(clamp, ramp_samples_);
        update_effective_cutoff();
        return true;
    }
    if (name == "resonance" || name == "res") {
        const float clamp = std::clamp(value, 0.0f, 1.0f);
        base_res_.set_target(clamp, ramp_samples_);
        res_ = base_res_.get();
        on_resonance_changed();
        return true;
    }
    if (name == "cutoff_cv") {
        cutoff_cv_ = value;
        update_effective_cutoff();
        return true;
    }
    if (name == "kybd_cv") {
        kybd_cv_ = value;
        update_effective_cutoff();
        return true;
    }
    if (name == "res_cv") {
        res_cv_accum_ = value;
        res_ = std::clamp(base_res_.get() + res_cv_accum_, 0.0f, 1.0f);
        on_resonance_changed();
        return true;
    }
    return false;
}

void VcfBase::update_effective_cutoff() {
    const float cv_sum = cutoff_cv_ + kybd_cv_;
    const float cur_cutoff = base_cutoff_.get();
    if (cur_cutoff == last_base_cutoff_ && cv_sum == last_cv_sum_) return;
    last_base_cutoff_ = cur_cutoff;
    last_cv_sum_      = cv_sum;
    float eff = std::max(20.0f, cur_cutoff * std::pow(2.0f, cv_sum));
    update_cutoff_coefficient(eff);
}

void VcfBase::do_pull(std::span<float> output, const VoiceContext* /*ctx*/) {
    const int n = static_cast<int>(output.size());
    if (base_cutoff_.is_ramping() || base_res_.is_ramping()) {
        base_cutoff_.advance(n);
        base_res_.advance(n);
        last_base_cutoff_ = -1.f; // force recompute
        update_effective_cutoff();
        res_ = std::clamp(base_res_.get() + res_cv_accum_, 0.0f, 1.0f);
        on_resonance_changed();
    } else {
        base_cutoff_.advance(n);
        base_res_.advance(n);
    }
    for (auto& s : output) process_sample(s);
}

void VcfBase::do_pull(AudioBuffer& output, const VoiceContext* /*ctx*/) {
    const int n = static_cast<int>(output.frames());
    if (base_cutoff_.is_ramping() || base_res_.is_ramping()) {
        base_cutoff_.advance(n);
        base_res_.advance(n);
        last_base_cutoff_ = -1.f;
        update_effective_cutoff();
        res_ = std::clamp(base_res_.get() + res_cv_accum_, 0.0f, 1.0f);
        on_resonance_changed();
    } else {
        base_cutoff_.advance(n);
        base_res_.advance(n);
    }
    for (size_t i = 0; i < output.frames(); ++i) {
        float s = (output.left[i] + output.right[i]) * 0.5f;
        process_sample(s);
        output.left[i] = output.right[i] = s;
    }
}

} // namespace audio
