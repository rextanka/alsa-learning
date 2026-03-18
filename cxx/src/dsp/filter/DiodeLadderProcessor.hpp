/**
 * @file DiodeLadderProcessor.hpp
 * @brief TB-style diode ladder filter — "rubbery acid" character.
 *
 * RT-SAFE chain node: PORT_AUDIO in → PORT_AUDIO out.
 * Type name: DIODE_FILTER
 *
 * TB-style character: 4-pole diode ladder with 3/4-pole blend at high resonance.
 * Component tolerances in the original circuit cause it to measure closer to
 * 18 dB/oct (3-pole) at moderate resonance. Emulated here by blending stage[2]
 * (3-pole) and stage[3] (4-pole) with the blend shifting 100% to 3-pole at
 * maximum resonance. Per-stage tanh saturation adds characteristic harmonic
 * distortion.
 */

#ifndef AUDIO_DIODE_LADDER_PROCESSOR_HPP
#define AUDIO_DIODE_LADDER_PROCESSOR_HPP

#include "LadderVcfBase.hpp"

namespace audio {

class DiodeLadderProcessor : public LadderVcfBase {
public:
    explicit DiodeLadderProcessor(int sample_rate)
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
        // TB-style pole blend: at full resonance, 3-pole (stage[2]) dominates
        float blend   = 1.0f - res_;
        float tap     = stage_[2] + blend * (stage_[3] - stage_[2]);
        float feedback = std::tanh(tap * res_ * 3.5f);
        float input    = sample - feedback;

        float s0 = stage_[0], s1 = stage_[1], s2 = stage_[2], s3 = stage_[3];
        stage_[0] += g_ * (std::tanh(input)     - std::tanh(s0));
        stage_[1] += g_ * (std::tanh(stage_[0]) - std::tanh(s1));
        stage_[2] += g_ * (std::tanh(stage_[1]) - std::tanh(s2));
        stage_[3] += g_ * (std::tanh(stage_[2]) - std::tanh(s3));

        float out3 = stage_[2];
        float out4 = stage_[3];
        sample = out3 + blend * (out4 - out3);

        if (std::isnan(sample) || std::isinf(sample)) { reset_state(); sample = 0.0f; }
    }

private:
    float hpf_cutoff_ = 0.0f;
};

} // namespace audio

#endif // AUDIO_DIODE_LADDER_PROCESSOR_HPP
