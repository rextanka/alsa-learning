/**
 * @file AudioMixerProcessor.cpp
 * @brief Audio mixer do_pull and self-registration.
 */
#include "AudioMixerProcessor.hpp"
#include "../../core/ModuleRegistry.hpp"
#include <algorithm>

namespace audio {

AudioMixerProcessor::AudioMixerProcessor(int sample_rate) {
    ramp_samples_ = static_cast<int>(static_cast<float>(sample_rate) * kRampSeconds);

    for (int i = 0; i < kMaxInputs; ++i) {
        const std::string idx = std::to_string(i + 1);
        declare_port({"audio_in_" + idx, PORT_AUDIO, PortDirection::IN});
    }
    declare_port({"audio_out", PORT_AUDIO, PortDirection::OUT});

    for (int i = 0; i < kMaxInputs; ++i) {
        const std::string idx = std::to_string(i + 1);
        declare_parameter({"gain_" + idx, "Input " + idx + " Gain", 0.0f, 1.0f, 1.0f});
    }
}

bool AudioMixerProcessor::apply_parameter(const std::string& name, float value) {
    for (int i = 0; i < kMaxInputs; ++i) {
        if (name == "gain_" + std::to_string(i + 1)) {
            gains_[i].set_target(std::clamp(value, 0.0f, 1.0f), ramp_samples_);
            return true;
        }
    }
    return false;
}

void AudioMixerProcessor::inject_audio(std::string_view port_name,
                                        std::span<const float> audio) {
    for (int i = 0; i < kMaxInputs; ++i) {
        if (port_name == "audio_in_" + std::to_string(i + 1)) {
            inputs_audio_[i] = audio;
            return;
        }
    }
}

void AudioMixerProcessor::do_pull(std::span<float> output, const VoiceContext*) {
    std::fill(output.begin(), output.end(), 0.0f);

    const size_t n = output.size();
    for (int i = 0; i < kMaxInputs; ++i) {
        gains_[i].advance(static_cast<int>(n));
        const float g = gains_[i].get();
        if (inputs_audio_[i].empty() || g < 1e-6f) continue;

        const size_t to_mix = std::min(n, inputs_audio_[i].size());
        for (size_t j = 0; j < to_mix; ++j)
            output[j] += g * inputs_audio_[i][j];
    }

    for (float& s : output)
        s = std::clamp(s, -1.0f, 1.0f);

    for (auto& span : inputs_audio_) span = {};
}

namespace {
[[maybe_unused]] const bool kRegistered = ModuleRegistry::instance().register_module(
    "AUDIO_MIXER",
    "4-input audio summing mixer with per-input gain",
    "Connect up to 4 audio sources to audio_in_1..4. Set gain_N to balance levels. "
    "Use for dual-VCO additive synthesis (saw+noise, triangle+sub). "
    "Default gain=1.0 — lower to 0.5 per input to avoid clipping on 2+ sources.",
    [](int sr) { return std::make_unique<AudioMixerProcessor>(sr); }
);
} // namespace

} // namespace audio
