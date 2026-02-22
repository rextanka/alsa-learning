/**
 * @file ADEnvelopeProcessor.hpp
 * @brief Attack-Decay envelope for percussive sounds.
 */

#ifndef AUDIO_AD_ENVELOPE_PROCESSOR_HPP
#define AUDIO_AD_ENVELOPE_PROCESSOR_HPP

#include "EnvelopeProcessor.hpp"
#include <algorithm>

namespace audio {

/**
 * @brief AD Envelope Processor.
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
        , attack_time_(0.01f)
        , decay_time_(0.1f)
    {
        update_rates();
    }

    void gate_on() override {
        state_ = State::Attack;
        update_rates();
    }

    void gate_off() override {
        // AD envelope ignores gate off (finishes Decay)
    }

    bool is_active() const override {
        return state_ != State::Idle;
    }

    bool is_releasing() const override {
        return state_ == State::Decay; // For AD, Decay is the final release phase
    }

    void reset() override {
        state_ = State::Idle;
        current_level_ = 0.0f;
    }

    void set_attack_time(float seconds) { attack_time_ = std::max(0.001f, seconds); update_rates(); }
    void set_decay_time(float seconds) { decay_time_ = std::max(0.001f, seconds); update_rates(); }

protected:
    void do_pull(std::span<float> output, const VoiceContext* /* voice_context */ = nullptr) override {
        for (auto& sample : output) {
            sample = process_sample();
        }
    }

private:
    float process_sample() {
        switch (state_) {
            case State::Attack:
                current_level_ += attack_rate_;
                if (current_level_ >= 1.0f) {
                    current_level_ = 1.0f;
                    state_ = State::Decay;
                }
                break;

            case State::Decay:
                current_level_ -= decay_rate_;
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
        attack_rate_ = 1.0f / (attack_time_ * sample_rate_);
        decay_rate_ = 1.0f / (decay_time_ * sample_rate_);
    }

    int sample_rate_;
    State state_;
    float current_level_;
    float attack_time_;
    float decay_time_;
    float attack_rate_;
    float decay_rate_;
};

} // namespace audio

#endif // AUDIO_AD_ENVELOPE_PROCESSOR_HPP
