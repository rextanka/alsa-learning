/**
 * @file MoogLadderProcessor.hpp
 * @brief 4-pole transistor ladder filter (Moog style), 24 dB/oct low-pass.
 *
 * RT-SAFE chain node: PORT_AUDIO in → PORT_AUDIO out.
 * Type name: MOOG_FILTER
 *
 * Character: smooth, creamy, "thick" — full tanh saturation in the feedback
 * path and linear-but-warm stage updates give the classic Moog-style growl.
 * Self-oscillates at resonance ≈ 1.0. Ringing transients at high resonance.
 */

#ifndef AUDIO_MOOG_LADDER_PROCESSOR_HPP
#define AUDIO_MOOG_LADDER_PROCESSOR_HPP

#include "LadderVcfBase.hpp"

namespace audio {

class MoogLadderProcessor : public LadderVcfBase {
public:
    explicit MoogLadderProcessor(int sample_rate)
        : LadderVcfBase(sample_rate)
    {
        declare_parameter({"hpf_cutoff", "HPF Stage (0-3)", 0.0f, 3.0f, 0.0f});
    }

    bool apply_parameter(const std::string& name, float value) override {
        if (name == "hpf_cutoff") { hpf_cutoff_ = value; return true; }
        return LadderVcfBase::apply_parameter(name, value);
    }

protected:
    void process_sample(float& sample) override {
        // Tanh-saturated feedback — the Moog-style signature
        float feedback = stage_[3] * res_ * 4.0f;
        float input    = sample - std::tanh(feedback);

        stage_[0] += g_ * (input     - stage_[0]);
        stage_[1] += g_ * (stage_[0] - stage_[1]);
        stage_[2] += g_ * (stage_[1] - stage_[2]);
        stage_[3] += g_ * (stage_[2] - stage_[3]);

        sample = stage_[3];
    }

private:
    float hpf_cutoff_ = 0.0f;
};

} // namespace audio

#endif // AUDIO_MOOG_LADDER_PROCESSOR_HPP
