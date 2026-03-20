/**
 * @file ADEnvelopeProcessor.hpp
 * @brief Attack-Decay envelope for percussive sounds.
 *
 * AD ignores gate_off — it completes the Decay stage regardless of note duration.
 * Exponential curves for both Attack and Decay using a one-pole IIR filter.
 * Each stage reaches ~99% of its target in the specified time.
 */

#ifndef AUDIO_AD_ENVELOPE_PROCESSOR_HPP
#define AUDIO_AD_ENVELOPE_PROCESSOR_HPP

#include "EnvelopeProcessor.hpp"
#include <algorithm>
#include <cmath>

namespace audio {

class ADEnvelopeProcessor : public EnvelopeProcessor {
public:
    enum class State { Idle, Attack, Decay };

    explicit ADEnvelopeProcessor(int sample_rate);

    void gate_on() override;
    void gate_off() override {} // AD ignores gate_off

    void on_note_on(double /*frequency*/) override { gate_on(); }
    void on_note_off() override {}

    bool is_active()    const override { return state_ != State::Idle; }
    bool is_releasing() const override { return state_ == State::Decay; }

    PortType output_port_type() const override { return PortType::PORT_CONTROL; }

    void reset() override { state_ = State::Idle; current_level_ = 0.0f; }

    bool apply_parameter(const std::string& name, float value) override;

protected:
    void do_pull(std::span<float> output, const VoiceContext* ctx = nullptr) override;

private:
    float process_sample();
    void  update_rates();

    static constexpr float kAttackTarget =  1.001f;
    static constexpr float kDecayTarget  = -0.001f;

    int   sample_rate_;
    State state_;
    float current_level_;

    float attack_time_ = 0.01f;
    float decay_time_  = 0.1f;

    float attack_coeff_ = 0.0f;
    float decay_coeff_  = 0.0f;
};

} // namespace audio

#endif // AUDIO_AD_ENVELOPE_PROCESSOR_HPP
