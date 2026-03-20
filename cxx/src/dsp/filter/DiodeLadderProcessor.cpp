/**
 * @file DiodeLadderProcessor.cpp
 * @brief Diode ladder constructor, process_sample, and self-registration.
 */
#include "DiodeLadderProcessor.hpp"
#include "../../core/ModuleRegistry.hpp"
#include <cmath>

namespace audio {

DiodeLadderProcessor::DiodeLadderProcessor(int sample_rate)
    : LadderVcfBase(sample_rate)
{
    declare_parameter({"env_depth", "Envelope Depth (oct/V)", 0.0f, 6.0f, 3.0f});

    // HPF in feedback path at ~100 Hz (impulse invariant, 1× rate)
    g_hpf_ = 1.0f - std::exp(-static_cast<float>(2.0 * M_PI) * 100.0f
                              / static_cast<float>(sample_rate));
}

bool DiodeLadderProcessor::apply_parameter(const std::string& name, float value) {
    if (name == "env_depth") {
        env_depth_ = std::clamp(value, 0.0f, 6.0f);
        return true;
    }
    if (name == "cutoff_cv") {
        return LadderVcfBase::apply_parameter("cutoff_cv", value * env_depth_);
    }
    return LadderVcfBase::apply_parameter(name, value);
}

void DiodeLadderProcessor::update_cutoff_coefficient(float cutoff) {
    g_ = std::tan(static_cast<float>(M_PI) * cutoff / static_cast<float>(sample_rate_));
    g_ = std::clamp(g_, 0.0001f, 0.9999f);
}

void DiodeLadderProcessor::process_sample(float& sample) {
    // HPF in feedback path — eliminates resonance at low cutoff (squelch)
    hpf_state_ += g_hpf_ * (stage_[3] - hpf_state_);
    const float hpf_out = stage_[3] - hpf_state_;

    // k=6 — self-oscillates at res≈0.72 for 4-pole at typical acid cutoffs
    const float feedback = std::tanh(hpf_out * res_ * 6.0f);
    const float input    = sample - feedback;

    stage_[0] += g_ * (std::tanh(input)     - std::tanh(stage_[0]));
    stage_[1] += g_ * (std::tanh(stage_[0]) - std::tanh(stage_[1]));
    stage_[2] += g_ * (std::tanh(stage_[1]) - std::tanh(stage_[2]));
    stage_[3] += g_ * (std::tanh(stage_[2]) - std::tanh(stage_[3]));

    sample = stage_[3];

    if (std::isnan(sample) || std::isinf(sample)) { reset_state(); sample = 0.0f; }
}

void DiodeLadderProcessor::reset_state() {
    LadderVcfBase::reset_state();
    hpf_state_ = 0.0f;
}

namespace {
[[maybe_unused]] const bool kRegistered = ModuleRegistry::instance().register_module(
    "DIODE_FILTER",
    "TB-style diode ladder LP — 3/4-pole blend gives 18–24 dB/oct rubbery acid character",
    "Use for TB-303 acid bass lines. env_depth [0, 6] scales envelope modulation depth. "
    "resonance > 0.9 gives the squeaky acid resonance. "
    "Pairs well with ADSR (fast attack, fast decay, zero sustain) for the classic acid squelch.",
    [](int sr) { return std::make_unique<DiodeLadderProcessor>(sr); }
);
} // namespace

} // namespace audio
