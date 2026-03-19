/**
 * @file DiodeLadderProcessor.hpp
 * @brief TB-303 style diode ladder filter — 4-pole 24dB/oct.
 *
 * RT-SAFE chain node: PORT_AUDIO in → PORT_AUDIO out.
 * Type name: DIODE_FILTER
 *
 * Stripped to minimum for verified self-oscillation:
 *   - 1× sample rate (g_ at fs — no oversampling complication)
 *   - k=6: for 4-pole, threshold is k·|H|⁴=1 where |H|⁴≈0.23 at the
 *     oscillation frequency → threshold k≈4.35; k=6 gives res threshold ≈0.72
 *   - HPF in feedback path (~100Hz) — creates squelch at low cutoff
 *   - Pure tanh everywhere — maximum small-signal loop gain
 *   - env_depth: scales cutoff_cv for multi-octave envelope sweeps
 *
 * Note: self-oscillation frequency ≈ 0.54×cutoff (at fc=800Hz, rings at ~430Hz)
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
        declare_parameter({"env_depth", "Envelope Depth (oct/V)", 0.0f, 6.0f, 3.0f});

        // HPF in feedback path at ~100Hz (impulse invariant, 1× rate)
        g_hpf_ = 1.0f - std::exp(-static_cast<float>(2.0 * M_PI) * 100.0f
                                  / static_cast<float>(sample_rate));
    }

    bool apply_parameter(const std::string& name, float value) override {
        if (name == "env_depth") {
            env_depth_ = std::clamp(value, 0.0f, 6.0f);
            return true;
        }
        if (name == "cutoff_cv") {
            return LadderVcfBase::apply_parameter("cutoff_cv", value * env_depth_);
        }
        return LadderVcfBase::apply_parameter(name, value);
    }

protected:
    // 1× sample rate
    void update_cutoff_coefficient(float cutoff) override {
        g_ = std::tan(static_cast<float>(M_PI) * cutoff
                      / static_cast<float>(sample_rate_));
        g_ = std::clamp(g_, 0.0001f, 0.9999f);
    }

    void process_sample(float& sample) override {
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

    void reset_state() override {
        LadderVcfBase::reset_state();
        hpf_state_ = 0.0f;
    }

private:
    float env_depth_ = 3.0f;
    float g_hpf_     = 0.0f;
    float hpf_state_ = 0.0f;
};

} // namespace audio

#endif // AUDIO_DIODE_LADDER_PROCESSOR_HPP
