/**
 * @file NoiseGateProcessor.cpp
 * @brief Noise gate do_pull and self-registration.
 */
#include "NoiseGateProcessor.hpp"
#include "../../core/ModuleRegistry.hpp"
#include <cmath>

namespace audio {

NoiseGateProcessor::NoiseGateProcessor(int sample_rate)
    : sample_rate_(sample_rate)
{
    declare_port({"audio_in",  PORT_AUDIO, PortDirection::IN});
    declare_port({"audio_out", PORT_AUDIO, PortDirection::OUT});

    declare_parameter({"threshold", "Threshold",       0.0f, 1.0f, 0.02f});
    declare_parameter({"attack",    "Gate Attack (s)", 0.0f, 0.1f, 0.001f});
    declare_parameter({"decay",     "Gate Decay (s)",  0.0f, 2.0f, 0.1f});

    update_coefficients();
}

bool NoiseGateProcessor::apply_parameter(const std::string& name, float value) {
    if (name == "threshold") { threshold_ = std::clamp(value, 0.0f, 1.0f);      return true; }
    if (name == "attack")    { attack_s_  = std::clamp(value, 0.0001f, 0.1f); update_coefficients(); return true; }
    if (name == "decay")     { decay_s_   = std::clamp(value, 0.001f, 2.0f);  update_coefficients(); return true; }
    return false;
}

void NoiseGateProcessor::do_pull(std::span<float> output, const VoiceContext* /*ctx*/) {
    for (auto& s : output) {
        const float abs_s = std::fabs(s);
        if (abs_s > envelope_)
            envelope_ = abs_s;
        else
            envelope_ *= env_decay_;

        const float target = envelope_ > threshold_ ? 1.0f : 0.0f;
        if (target > gain_)
            gain_ += attack_coeff_ * (target - gain_);
        else
            gain_ += decay_coeff_  * (target - gain_);

        s *= gain_;
    }
}

void NoiseGateProcessor::update_coefficients() {
    attack_coeff_ = 1.0f - std::exp(-1.0f / (attack_s_ * static_cast<float>(sample_rate_)));
    decay_coeff_  = 1.0f - std::exp(-1.0f / (decay_s_  * static_cast<float>(sample_rate_)));
    env_decay_    = std::exp(-1.0f / (0.05f * static_cast<float>(sample_rate_)));
}

namespace {
[[maybe_unused]] const bool kRegistered = ModuleRegistry::instance().register_module(
    "NOISE_GATE",
    "Threshold-based gate: opens on signal above threshold, closes on silence below it",
    "Insert at the end of a signal chain to silence hiss during rests. "
    "threshold [0, 1] sets the RMS level that opens the gate. "
    "attack/release control how fast the gate opens and closes.",
    [](int sr) { return std::make_unique<NoiseGateProcessor>(sr); }
);
} // namespace

} // namespace audio
