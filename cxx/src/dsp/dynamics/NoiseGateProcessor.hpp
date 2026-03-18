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
 *
 * Modelled on the Boss NF-1 Noise Gate (Roland Recording §4-5, Fig 4-4).
 */

#ifndef NOISE_GATE_PROCESSOR_HPP
#define NOISE_GATE_PROCESSOR_HPP

#include "../Processor.hpp"
#include <algorithm>
#include <cmath>

namespace audio {

class NoiseGateProcessor : public Processor {
public:
    explicit NoiseGateProcessor(int sample_rate)
        : sample_rate_(sample_rate)
    {
        declare_port({"audio_in",  PORT_AUDIO, PortDirection::IN});
        declare_port({"audio_out", PORT_AUDIO, PortDirection::OUT});

        declare_parameter({"threshold", "Threshold",       0.0f, 1.0f, 0.02f});
        declare_parameter({"attack",    "Gate Attack (s)", 0.0f, 0.1f, 0.001f});
        declare_parameter({"decay",     "Gate Decay (s)",  0.0f, 2.0f, 0.1f});

        update_coefficients();
    }

    void reset() override { envelope_ = 0.0f; gain_ = 0.0f; }

    bool apply_parameter(const std::string& name, float value) override {
        if (name == "threshold") { threshold_ = std::clamp(value, 0.0f, 1.0f); return true; }
        if (name == "attack")    { attack_s_  = std::clamp(value, 0.0001f, 0.1f); update_coefficients(); return true; }
        if (name == "decay")     { decay_s_   = std::clamp(value, 0.001f, 2.0f);  update_coefficients(); return true; }
        return false;
    }

    PortType output_port_type() const override { return PortType::PORT_AUDIO; }

protected:
    void do_pull(std::span<float> output,
                 const VoiceContext* /*ctx*/ = nullptr) override {
        for (auto& s : output) {
            // Peak envelope detector
            const float abs_s = std::fabs(s);
            if (abs_s > envelope_)
                envelope_ = abs_s;
            else
                envelope_ *= env_decay_;

            // Gate: open when envelope above threshold, close otherwise
            const float target = envelope_ > threshold_ ? 1.0f : 0.0f;
            if (target > gain_)
                gain_ += attack_coeff_ * (target - gain_);
            else
                gain_ += decay_coeff_  * (target - gain_);

            s *= gain_;
        }
    }

private:
    void update_coefficients() {
        // IIR coefficients: approach target in attack_s_ / decay_s_ time constants
        attack_coeff_ = 1.0f - std::exp(-1.0f / (attack_s_ * static_cast<float>(sample_rate_)));
        decay_coeff_  = 1.0f - std::exp(-1.0f / (decay_s_  * static_cast<float>(sample_rate_)));
        // Envelope follower decay: fast peak hold, 50ms release
        env_decay_    = std::exp(-1.0f / (0.05f * static_cast<float>(sample_rate_)));
    }

    int   sample_rate_;
    float threshold_    = 0.02f;
    float attack_s_     = 0.001f;
    float decay_s_      = 0.1f;

    float attack_coeff_ = 0.0f;
    float decay_coeff_  = 0.0f;
    float env_decay_    = 0.0f;

    float envelope_     = 0.0f; // current detected envelope
    float gain_         = 0.0f; // current gate gain [0, 1]
};

} // namespace audio

#endif // NOISE_GATE_PROCESSOR_HPP
