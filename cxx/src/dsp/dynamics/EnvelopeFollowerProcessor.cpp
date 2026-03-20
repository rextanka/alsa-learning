/**
 * @file EnvelopeFollowerProcessor.cpp
 * @brief Envelope follower do_pull and self-registration.
 */
#include "EnvelopeFollowerProcessor.hpp"
#include "../../core/ModuleRegistry.hpp"
#include <cmath>

namespace audio {

EnvelopeFollowerProcessor::EnvelopeFollowerProcessor(int sample_rate)
    : sample_rate_(sample_rate)
{
    declare_port({"audio_in",     PORT_AUDIO,   PortDirection::IN});
    declare_port({"audio_out",    PORT_AUDIO,   PortDirection::OUT});
    declare_port({"envelope_out", PORT_CONTROL, PortDirection::OUT, true});

    declare_parameter({"attack",  "Attack (s)",  0.0f, 1.0f, 0.005f});
    declare_parameter({"release", "Release (s)", 0.0f, 2.0f, 0.1f});

    update_coefficients();
}

bool EnvelopeFollowerProcessor::apply_parameter(const std::string& name, float value) {
    if (name == "attack")  { attack_s_  = std::clamp(value, 0.0001f, 1.0f); update_coefficients(); return true; }
    if (name == "release") { release_s_ = std::clamp(value, 0.001f,  2.0f); update_coefficients(); return true; }
    return false;
}

void EnvelopeFollowerProcessor::do_pull(std::span<float> output, const VoiceContext* /*ctx*/) {
    for (const auto& s : output) {
        const float abs_s = std::fabs(s);
        const float coeff = abs_s > envelope_ ? attack_coeff_ : release_coeff_;
        envelope_ += coeff * (abs_s - envelope_);
    }
    // Passthrough — audio is unchanged
}

void EnvelopeFollowerProcessor::update_coefficients() {
    attack_coeff_  = 1.0f - std::exp(-1.0f / (attack_s_  * static_cast<float>(sample_rate_)));
    release_coeff_ = 1.0f - std::exp(-1.0f / (release_s_ * static_cast<float>(sample_rate_)));
}

namespace {
[[maybe_unused]] const bool kRegistered = ModuleRegistry::instance().register_module(
    "ENVELOPE_FOLLOWER",
    "Extracts a dynamic control signal (RMS envelope) from the audio input",
    "Insert in the signal chain as a transparent audio passthrough; "
    "its envelope_out can be connected to downstream CV targets "
    "(filter cutoff, VCA gain) for auto-wah or dynamic filtering effects. "
    "attack/release control the follower's tracking speed.",
    [](int sr) { return std::make_unique<EnvelopeFollowerProcessor>(sr); }
);
} // namespace

} // namespace audio
