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
 *
 * Example JSON usage (two parallel VCOs):
 * @code
 * "chain": [
 *   { "type": "COMPOSITE_GENERATOR", "tag": "VCO1" },
 *   { "type": "COMPOSITE_GENERATOR", "tag": "VCO2" },
 *   { "type": "AUDIO_MIXER",         "tag": "MIX"  },
 *   { "type": "MOOG_FILTER",         "tag": "VCF"  },
 *   { "type": "ADSR_ENVELOPE",       "tag": "ENV"  },
 *   { "type": "VCA",                 "tag": "VCA"  }
 * ],
 * "connections": [
 *   { "from_tag": "VCO1", "from_port": "audio_out", "to_tag": "MIX", "to_port": "audio_in_1" },
 *   { "from_tag": "VCO2", "from_port": "audio_out", "to_tag": "MIX", "to_port": "audio_in_2" },
 *   { "from_tag": "MIX",  "from_port": "audio_out", "to_tag": "VCF", "to_port": "audio_in"   }
 * ],
 * "parameters": {
 *   "VCO1": { "saw_gain": 1.0 },
 *   "VCO2": { ... },
 *   "MIX":  { "gain_1": 0.7, "gain_2": 0.3 }
 * }
 * @endcode
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
    static constexpr float kRampSeconds  = 0.010f; // 10 ms parameter smoothing

    explicit AudioMixerProcessor(int sample_rate = 48000) {
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

    void reset() override {
        for (auto& span : inputs_audio_) span = {};
    }

    bool apply_parameter(const std::string& name, float value) override {
        for (int i = 0; i < kMaxInputs; ++i) {
            if (name == "gain_" + std::to_string(i + 1)) {
                gains_[i].set_target(std::clamp(value, 0.0f, 1.0f), ramp_samples_);
                return true;
            }
        }
        return false;
    }

    void inject_audio(std::string_view port_name,
                      std::span<const float> audio) override {
        for (int i = 0; i < kMaxInputs; ++i) {
            if (port_name == "audio_in_" + std::to_string(i + 1)) {
                inputs_audio_[i] = audio;
                return;
            }
        }
    }

    PortType output_port_type() const override { return PortType::PORT_AUDIO; }

protected:
    void do_pull(std::span<float> output,
                 const VoiceContext* /*ctx*/ = nullptr) override {
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

        // Safety clamp — multiple loud sources can exceed ±1
        for (float& s : output)
            s = std::clamp(s, -1.0f, 1.0f);

        // Clear injected spans — valid for this block only
        for (auto& span : inputs_audio_) span = {};
    }

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
