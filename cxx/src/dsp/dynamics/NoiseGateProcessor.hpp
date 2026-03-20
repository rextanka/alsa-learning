/**
 * @file NoiseGateProcessor.hpp
 * @brief Noise gate — suppresses audio below a threshold.
 *
 * RT-SAFE chain node: PORT_AUDIO in → PORT_AUDIO out.
 * Type name: NOISE_GATE
 *
 * Uses a peak envelope detector on the input signal. When the detected
 * envelope rises above `threshold`, the gain ramps to 1.0 over `attack` time.
 * When the envelope falls below `threshold`, the gain ramps to 0.0 over
 * `decay` time. Output = audio_in * gain.
 */

#ifndef NOISE_GATE_PROCESSOR_HPP
#define NOISE_GATE_PROCESSOR_HPP

#include "../Processor.hpp"
#include <algorithm>
#include <cmath>

namespace audio {

class NoiseGateProcessor : public Processor {
public:
    explicit NoiseGateProcessor(int sample_rate);

    void reset() override { envelope_ = 0.0f; gain_ = 0.0f; }

    bool apply_parameter(const std::string& name, float value) override;

    PortType output_port_type() const override { return PortType::PORT_AUDIO; }

protected:
    void do_pull(std::span<float> output, const VoiceContext* ctx = nullptr) override;

private:
    void update_coefficients();

    int   sample_rate_;
    float threshold_    = 0.02f;
    float attack_s_     = 0.001f;
    float decay_s_      = 0.1f;

    float attack_coeff_ = 0.0f;
    float decay_coeff_  = 0.0f;
    float env_decay_    = 0.0f;

    float envelope_     = 0.0f;
    float gain_         = 0.0f;
};

} // namespace audio

#endif // NOISE_GATE_PROCESSOR_HPP
