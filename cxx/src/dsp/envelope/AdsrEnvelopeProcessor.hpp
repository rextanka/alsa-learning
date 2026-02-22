/**
 * @file AdsrEnvelopeProcessor.hpp
 * @brief Concrete implementation of an ADSR (Attack, Decay, Sustain, Release) envelope.
 * 
 * This file follows the project rules defined in .cursorrules:
 * - Separation of Concerns: Core DSP logic separated from hardware/OS audio code.
 * - Modern C++: Target C++20/23 for all new code.
 */

#ifndef ADSR_ENVELOPE_PROCESSOR_HPP
#define ADSR_ENVELOPE_PROCESSOR_HPP

#include "EnvelopeProcessor.hpp"
#include "Logger.hpp"
#include <atomic>
#include <span>
#include <algorithm>

namespace audio {

/**
 * @brief ADSR Envelope Processor.
 * 
 * Implements a standard four-stage envelope with linear ramps for A, D, and R.
 */
class AdsrEnvelopeProcessor : public EnvelopeProcessor {
public:
    /**
     * @brief ADSR stages.
     */
    enum class State {
        Idle,
        Attack,
        Decay,
        Sustain,
        Release
    };

    explicit AdsrEnvelopeProcessor(int sample_rate)
        : sample_rate_(sample_rate)
        , state_(State::Idle)
        , current_level_(0.0f)
        , attack_time_(0.01f)
        , decay_time_(0.1f)
        , sustain_level_(0.7f)
        , release_time_(0.2f)
    {
        update_rates();
    }

    void gate_on() override {
        AudioLogger::instance().log_message("ADSR", "Gate On");
        state_ = State::Attack;
        // Start from current level to avoid clicks if re-triggered
        update_rates();
    }

    void gate_off() override {
        if (state_ != State::Idle) {
            state_ = State::Release;
            update_rates();
        }
    }

    bool is_active() const override {
        return state_ != State::Idle;
    }

    bool is_releasing() const override {
        return state_ == State::Release;
    }

    void reset() override {
        state_ = State::Idle;
        current_level_ = 0.0f;
    }

    // Setters for envelope parameters
    void set_attack_time(float seconds) { attack_time_ = std::max(0.001f, seconds); update_rates(); }
    void set_decay_time(float seconds) { decay_time_ = std::max(0.001f, seconds); update_rates(); }
    void set_sustain_level(float level) { sustain_level_ = std::clamp(level, 0.0f, 1.0f); update_rates(); }
    void set_release_time(float seconds) { release_time_ = std::max(0.001f, seconds); update_rates(); }

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
                if (current_level_ <= sustain_level_) {
                    current_level_ = sustain_level_;
                    state_ = State::Sustain;
                }
                break;

            case State::Sustain:
                current_level_ = sustain_level_;
                break;

            case State::Release:
                current_level_ -= release_rate_;
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
        decay_rate_ = (1.0f - sustain_level_) / (decay_time_ * sample_rate_);
        release_rate_ = sustain_level_ / (release_time_ * sample_rate_);
    }

    int sample_rate_;
    State state_;
    float current_level_;

    float attack_time_;
    float decay_time_;
    float sustain_level_;
    float release_time_;

    float attack_rate_;
    float decay_rate_;
    float release_rate_;
};

} // namespace audio

#endif // ADSR_ENVELOPE_PROCESSOR_HPP
