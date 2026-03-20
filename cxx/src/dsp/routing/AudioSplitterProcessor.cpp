/**
 * @file AudioSplitterProcessor.cpp
 * @brief Audio splitter do_pull and self-registration.
 */
#include "AudioSplitterProcessor.hpp"
#include "../../core/ModuleRegistry.hpp"

namespace audio {

AudioSplitterProcessor::AudioSplitterProcessor(int sample_rate) {
    ramp_samples_ = static_cast<int>(static_cast<float>(sample_rate) * kRampSeconds);
    declare_port({"audio_in",    PORT_AUDIO, PortDirection::IN});
    declare_port({"audio_out_1", PORT_AUDIO, PortDirection::OUT});
    declare_port({"audio_out_2", PORT_AUDIO, PortDirection::OUT});
    declare_port({"audio_out_3", PORT_AUDIO, PortDirection::OUT});
    declare_port({"audio_out_4", PORT_AUDIO, PortDirection::OUT});

    declare_parameter({"gain_1", "Output 1 Gain", 0.0f, 2.0f, 1.0f});
    declare_parameter({"gain_2", "Output 2 Gain", 0.0f, 2.0f, 1.0f});
    declare_parameter({"gain_3", "Output 3 Gain", 0.0f, 2.0f, 1.0f});
    declare_parameter({"gain_4", "Output 4 Gain", 0.0f, 2.0f, 1.0f});
}

void AudioSplitterProcessor::do_pull(std::span<float> output, const VoiceContext*) {
    const int n_frames = static_cast<int>(output.size());
    gain_[0].advance(n_frames);
    gain_[1].advance(n_frames);
    gain_[2].advance(n_frames);
    gain_[3].advance(n_frames);

    const float g0 = gain_[0].get();
    if (!audio_in_.empty()) {
        const size_t n = std::min(output.size(), audio_in_.size());
        for (size_t i = 0; i < n; ++i) output[i] = audio_in_[i] * g0;
        audio_in_ = {};
    } else {
        for (auto& s : output) s *= g0;
    }
}

namespace {
[[maybe_unused]] const bool kRegistered = ModuleRegistry::instance().register_module(
    "AUDIO_SPLITTER",
    "1-to-4 audio fan-out — routes one signal to up to 4 downstream nodes",
    "Connect audio_out_1 to the primary chain; audio_out_2 → RING_MOD:audio_in_b "
    "for secondary audio injection. gain_N scales each output independently. "
    "Full per-output parallel routing requires Phase 20+ parallel executor.",
    [](int sr) { return std::make_unique<AudioSplitterProcessor>(sr); }
);
} // namespace

} // namespace audio
