/**
 * @file ADEnvelopeProcessor.hpp
 * @brief Attack-Decay envelope for percussive sounds.
 */

#ifndef AUDIO_AD_ENVELOPE_PROCESSOR_HPP
#define AUDIO_AD_ENVELOPE_PROCESSOR_HPP

#include "EnvelopeProcessor.hpp"
#include <algorithm>
#include <cmath>

namespace audio {

/**
 * @brief AD Envelope Processor.
 *
 * Exponential curves for both Attack and Decay using a one-pole IIR filter.
 * Each stage reaches ~99% of its target in the specified time.
 *
 * AD ignores gate_off — it completes the Decay stage regardless of note duration.
 */
class ADEnvelopeProcessor : public EnvelopeProcessor {
public:
    enum class State {
        Idle,
        Attack,
        Decay
    };

    explicit ADEnvelopeProcessor(int sample_rate)
        : sample_rate_(sample_rate)
        , state_(State::Idle)
        , current_level_(0.0f)
    {
        update_rates();

        declare_port({"gate_in",      PORT_CONTROL, PortDirection::IN,  true});
        declare_port({"envelope_out", PORT_CONTROL, PortDirection::OUT, true});

        declare_parameter({"attack", "Attack Time", 0.0f, 10.0f, 0.01f, true});
        declare_parameter({"decay",  "Decay Time",  0.0f, 10.0f, 0.10f, true});
    }

    void gate_on() override {
        state_ = State::Attack;
        update_rates();
    }

    void gate_off() override {
        // AD envelope ignores gate off (finishes Decay)
    }

    void on_note_on(double /*frequency*/) override { gate_on(); }
    void on_note_off() override {}

    bool is_active() const override {
        return state_ != State::Idle;
    }

    bool is_releasing() const override {
        return state_ == State::Decay; // For AD, Decay is the final release phase
    }

    PortType output_port_type() const override { return PortType::PORT_CONTROL; }

    void reset() override {
        state_ = State::Idle;
        current_level_ = 0.0f;
    }

    bool apply_parameter(const std::string& name, float value) override {
        if (name == "attack") { attack_time_ = std::max(0.001f, value); update_rates(); return true; }
        if (name == "decay")  { decay_time_  = std::max(0.001f, value); update_rates(); return true; }
        return false;
    }

protected:
    void do_pull(std::span<float> output, const VoiceContext* /* voice_context */ = nullptr) override {
        for (auto& sample : output) {
            sample = process_sample();
        }
    }

private:
    static constexpr float kAttackTarget =  1.001f;
    static constexpr float kDecayTarget  = -0.001f;

    float process_sample() {
        switch (state_) {
            case State::Attack:
                current_level_ = attack_coeff_ * current_level_
                                + (1.0f - attack_coeff_) * kAttackTarget;
                if (current_level_ >= 1.0f) {
                    current_level_ = 1.0f;
                    state_ = State::Decay;
                }
                break;

            case State::Decay:
                current_level_ = decay_coeff_ * current_level_
                                + (1.0f - decay_coeff_) * kDecayTarget;
                if (current_level_ <= 0.0f) {
                    current_level_ = 0.0f;
                    state_ = State::Idle;
                }
                break;

            case State::Idle:
                current_level_ = 0.0f;
                break;
        }
        return current_level_;
    }

    void update_rates() {
        static constexpr float kLog9 = 2.197224577f;
        const float sr = static_cast<float>(sample_rate_);
        attack_coeff_ = std::exp(-kLog9 / (attack_time_ * sr));
        decay_coeff_  = std::exp(-kLog9 / (decay_time_  * sr));
    }

    int   sample_rate_;
    State state_;
    float current_level_;

    // Plain floats — no SmoothedParam. IIR curves don't need additional smoothing.
    float attack_time_ = 0.01f;
    float decay_time_  = 0.1f;

    float attack_coeff_ = 0.0f;
    float decay_coeff_  = 0.0f;
};

} // namespace audio

#endif // AUDIO_AD_ENVELOPE_PROCESSOR_HPP
