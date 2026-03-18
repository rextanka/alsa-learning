/**
 * @file EnvelopeFollowerProcessor.hpp
 * @brief Envelope follower — extracts a dynamic control signal from audio.
 *
 * RT-SAFE chain node: PORT_AUDIO in → PORT_AUDIO out (transparent passthrough).
 * Type name: ENVELOPE_FOLLOWER
 *
 * Sits in signal_chain_ as a transparent audio passthrough. After each block,
 * Voice::pull_mono detects this node via dynamic_cast and injects a constant-fill
 * ctrl_span (value = get_envelope()) into the executor's ctrl_spans[] table.
 * This makes `envelope_out` available to CV dispatch: wire it like any CV output:
 *   engine_connect_ports(h, "EF", "envelope_out", "VCA", "gain_cv")
 *
 * Connection ordering matters: the EnvelopeFollower node must appear before the
 * CV consumer (e.g. VCA) in the signal_chain_ so that the ctrl_span is populated
 * before the consumer's CV dispatch runs.
 */

#ifndef ENVELOPE_FOLLOWER_PROCESSOR_HPP
#define ENVELOPE_FOLLOWER_PROCESSOR_HPP

#include "../Processor.hpp"
#include <algorithm>
#include <cmath>

namespace audio {

class EnvelopeFollowerProcessor : public Processor {
public:
    explicit EnvelopeFollowerProcessor(int sample_rate)
        : sample_rate_(sample_rate)
    {
        declare_port({"audio_in",      PORT_AUDIO,   PortDirection::IN});
        declare_port({"audio_out",     PORT_AUDIO,   PortDirection::OUT});
        declare_port({"envelope_out",  PORT_CONTROL, PortDirection::OUT, true}); // unipolar

        declare_parameter({"attack",  "Attack (s)",  0.0f, 1.0f, 0.005f});
        declare_parameter({"release", "Release (s)", 0.0f, 2.0f, 0.1f});

        update_coefficients();
    }

    void reset() override { envelope_ = 0.0f; }

    bool apply_parameter(const std::string& name, float value) override {
        if (name == "attack")  { attack_s_  = std::clamp(value, 0.0001f, 1.0f); update_coefficients(); return true; }
        if (name == "release") { release_s_ = std::clamp(value, 0.001f,  2.0f); update_coefficients(); return true; }
        return false;
    }

    /** Read the most recently computed envelope value [0, 1]. */
    float get_envelope() const { return envelope_; }

    PortType output_port_type() const override { return PortType::PORT_AUDIO; }

protected:
    void do_pull(std::span<float> output,
                 const VoiceContext* /*ctx*/ = nullptr) override {
        for (const auto& s : output) {
            const float abs_s = std::fabs(s);
            const float coeff = abs_s > envelope_ ? attack_coeff_ : release_coeff_;
            envelope_ += coeff * (abs_s - envelope_);
        }
        // Passthrough — audio is unchanged
    }

private:
    void update_coefficients() {
        attack_coeff_  = 1.0f - std::exp(-1.0f / (attack_s_  * static_cast<float>(sample_rate_)));
        release_coeff_ = 1.0f - std::exp(-1.0f / (release_s_ * static_cast<float>(sample_rate_)));
    }

    int   sample_rate_;
    float attack_s_       = 0.005f;
    float release_s_      = 0.1f;
    float attack_coeff_   = 0.0f;
    float release_coeff_  = 0.0f;
    float envelope_       = 0.0f; ///< current envelope value [0, 1]
};

} // namespace audio

#endif // ENVELOPE_FOLLOWER_PROCESSOR_HPP
