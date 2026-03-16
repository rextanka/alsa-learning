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
#include <cmath>

namespace audio {

/**
 * @brief ADSR Envelope Processor.
 *
 * Output contract (MODULE_DESC §3, VCA/Envelope separation rule):
 *   do_pull() fills the output span with per-sample envelope level values
 *   in the range [0.0, 1.0].  It does NOT multiply audio in-place.
 *   Audio amplitude scaling is performed by VcaProcessor::apply().
 *
 * Implements a standard four-stage envelope with exponential curves for A, D,
 * and R segments using a one-pole IIR filter per stage. Each coefficient is
 * computed so the stage reaches ~99% of its target in the specified time.
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

    /**
     * @brief Get the current output level of the envelope.
     */
    float get_level() const { return current_level_; }

protected:
    void do_pull(std::span<float> output, const VoiceContext* /* voice_context */ = nullptr) override {
        // RT-SAFE: fill output with per-sample envelope level (PORT_CONTROL signal).
        // Must NOT multiply audio in-place — that is VcaProcessor's responsibility.
        for (auto& sample : output) {
            sample = process_sample();
        }
    }

private:
    // Exponential one-pole IIR per stage.
    // Each stage approaches its target asymptotically; a small overshoot/undershoot
    // on the target ensures the termination threshold is reliably crossed.
    static constexpr float kAttackTarget  =  1.001f; // slightly above 1 → crosses 1.0
    static constexpr float kReleaseTarget = -0.001f; // slightly below 0 → crosses 0.0

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

            case State::Decay: {
                // Target slightly below sustain so the level crosses the threshold.
                const float decay_target = sustain_level_ - 0.001f;
                current_level_ = decay_coeff_ * current_level_
                                + (1.0f - decay_coeff_) * decay_target;
                if (current_level_ <= sustain_level_) {
                    current_level_ = sustain_level_;
                    state_ = State::Sustain;
                }
                break;
            }

            case State::Sustain:
                current_level_ = sustain_level_;
                break;

            case State::Release:
                current_level_ = release_coeff_ * current_level_
                                + (1.0f - release_coeff_) * kReleaseTarget;
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

    // Compute one-pole coefficients: coeff = exp(-log(9) / (time * sr))
    // This ensures the stage reaches ~99% of its target in `time` seconds.
    void update_rates() {
        static constexpr float kLog9 = 2.197224577f; // log(9)
        attack_coeff_  = std::exp(-kLog9 / (attack_time_  * static_cast<float>(sample_rate_)));
        decay_coeff_   = std::exp(-kLog9 / (decay_time_   * static_cast<float>(sample_rate_)));
        release_coeff_ = std::exp(-kLog9 / (release_time_ * static_cast<float>(sample_rate_)));
    }

    int sample_rate_;
    State state_;
    float current_level_;

    float attack_time_;
    float decay_time_;
    float sustain_level_;
    float release_time_;

    float attack_coeff_;
    float decay_coeff_;
    float release_coeff_;
};

} // namespace audio

#endif // ADSR_ENVELOPE_PROCESSOR_HPP
