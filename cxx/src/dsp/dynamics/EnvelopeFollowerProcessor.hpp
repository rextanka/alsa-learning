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
 */

#ifndef ENVELOPE_FOLLOWER_PROCESSOR_HPP
#define ENVELOPE_FOLLOWER_PROCESSOR_HPP

#include "../Processor.hpp"
#include <algorithm>
#include <cmath>

namespace audio {

class EnvelopeFollowerProcessor : public Processor {
public:
    explicit EnvelopeFollowerProcessor(int sample_rate);

    void reset() override { envelope_ = 0.0f; }

    bool apply_parameter(const std::string& name, float value) override;

    float get_envelope() const { return envelope_; }

    PortType output_port_type() const override { return PortType::PORT_AUDIO; }

protected:
    void do_pull(std::span<float> output, const VoiceContext* ctx = nullptr) override;

private:
    void update_coefficients();

    int   sample_rate_;
    float attack_s_      = 0.005f;
    float release_s_     = 0.1f;
    float attack_coeff_  = 0.0f;
    float release_coeff_ = 0.0f;
    float envelope_      = 0.0f;
};

} // namespace audio

#endif // ENVELOPE_FOLLOWER_PROCESSOR_HPP
