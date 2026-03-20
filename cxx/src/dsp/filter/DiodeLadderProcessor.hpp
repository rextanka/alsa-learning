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
    explicit DiodeLadderProcessor(int sample_rate);

    bool apply_parameter(const std::string& name, float value) override;

protected:
    void update_cutoff_coefficient(float cutoff) override;
    void process_sample(float& sample) override;
    void reset_state() override;

private:
    float env_depth_ = 3.0f;
    float g_hpf_     = 0.0f;
    float hpf_state_ = 0.0f;
};

} // namespace audio

#endif // AUDIO_DIODE_LADDER_PROCESSOR_HPP
