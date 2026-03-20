/**
 * @file AudioMixerProcessor.hpp
 * @brief N-input audio summing mixer for in-patch multi-source mixing.
 *
 * RT-SAFE chain node: up to 4 PORT_AUDIO inputs → PORT_AUDIO out.
 * Type name: AUDIO_MIXER
 *
 * Accepts up to 4 parallel audio inputs via inject_audio() and sums them
 * with individual gain scaling. This enables in-patch multi-source mixing:
 *
 *   - Multi-VCO additive synthesis (saw + noise layer, triangle + sub)
 *   - Parallel filter paths (noise + tonal, LP branch + HP branch)
 *   - Wet/dry mixing (dry signal + processed signal at reduced gain)
 *   - Layered percussion (VCO body + noise transient)
 *
 * Multi-input execution: each audio_in_N is injected via inject_audio()
 * by Voice::pull_mono before do_pull() is called. Non-injected inputs are
 * treated as silence (zero contribution).
 *
 * Ports:
 *   audio_in_1 .. audio_in_4  — PORT_AUDIO inputs (all optional)
 *   audio_out                 — PORT_AUDIO summed output
 *
 * Parameters:
 *   gain_1 .. gain_4 [0, 1] — per-input level (default 1.0)
 */

#ifndef AUDIO_MIXER_PROCESSOR_HPP
#define AUDIO_MIXER_PROCESSOR_HPP

#include "../Processor.hpp"
#include "../SmoothedParam.hpp"
#include <algorithm>
#include <array>

namespace audio {

class AudioMixerProcessor : public Processor {
public:
    static constexpr int   kMaxInputs    = 4;
    static constexpr float kRampSeconds  = 0.010f;

    explicit AudioMixerProcessor(int sample_rate = 48000);

    void reset() override { for (auto& span : inputs_audio_) span = {}; }

    bool apply_parameter(const std::string& name, float value) override;

    void inject_audio(std::string_view port_name,
                      std::span<const float> audio) override;

    PortType output_port_type() const override { return PortType::PORT_AUDIO; }

protected:
    void do_pull(std::span<float> output, const VoiceContext* ctx = nullptr) override;

private:
    int ramp_samples_ = 480;
    std::array<std::span<const float>, kMaxInputs> inputs_audio_{};
    std::array<SmoothedParam, kMaxInputs> gains_{
        SmoothedParam{1.0f}, SmoothedParam{1.0f},
        SmoothedParam{1.0f}, SmoothedParam{1.0f}
    };
};

} // namespace audio

#endif // AUDIO_MIXER_PROCESSOR_HPP
