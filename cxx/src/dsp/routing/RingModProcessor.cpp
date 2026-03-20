/**
 * @file RingModProcessor.cpp
 * @brief Ring modulator do_pull and self-registration.
 */
#include "RingModProcessor.hpp"
#include "../../core/ModuleRegistry.hpp"
#include <algorithm>

namespace audio {

RingModProcessor::RingModProcessor(int sample_rate) {
    ramp_samples_ = static_cast<int>(static_cast<float>(sample_rate) * kRampSeconds);
    declare_port({"audio_in_a", PORT_AUDIO,   PortDirection::IN});
    declare_port({"audio_in_b", PORT_AUDIO,   PortDirection::IN});
    declare_port({"audio_out",  PORT_AUDIO,   PortDirection::OUT});
    declare_port({"mod_in",     PORT_CONTROL, PortDirection::IN, false});

    declare_parameter({"mix", "Dry/Wet Mix", 0.0f, 1.0f, 1.0f});
}

void RingModProcessor::do_pull(std::span<float> output, const VoiceContext*) {
    mix_.advance(static_cast<int>(output.size()));
    const float mix_val = mix_.get();
    const float dry = 1.0f - mix_val;

    if (!audio_in_a_.empty() && !audio_in_b_.empty()) {
        const size_t n = std::min({output.size(), audio_in_a_.size(), audio_in_b_.size()});
        for (size_t i = 0; i < n; ++i) {
            const float wet = audio_in_a_[i] * audio_in_b_[i];
            output[i] = dry * audio_in_a_[i] + mix_val * wet;
        }
    } else if (!audio_in_a_.empty()) {
        const float mod = 1.0f + mod_cv_;
        const size_t n = std::min(output.size(), audio_in_a_.size());
        for (size_t i = 0; i < n; ++i) {
            const float wet = audio_in_a_[i] * mod;
            output[i] = dry * audio_in_a_[i] + mix_val * wet;
        }
    }

    audio_in_a_ = {};
    audio_in_b_ = {};
    mod_cv_     = 0.0f;
}

namespace {
[[maybe_unused]] const bool kRegistered = ModuleRegistry::instance().register_module(
    "RING_MOD",
    "4-quadrant ring modulator — A×B sidebands for bell/metallic/tremolo effects",
    "Connect VCO1:audio_out → RING_MOD:audio_in_a, VCO2:audio_out → RING_MOD:audio_in_b. "
    "VCO2 at an inharmonic interval (e.g. 7.0 semitones above) produces bell tones. "
    "Connect LFO:control_out → RING_MOD:mod_in for amplitude tremolo.",
    [](int sr) { return std::make_unique<RingModProcessor>(sr); }
);
} // namespace

} // namespace audio
