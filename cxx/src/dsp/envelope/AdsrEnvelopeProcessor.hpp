/**
 * @file AdsrEnvelopeProcessor.hpp
 * @brief Concrete implementation of an ADSR envelope generator.
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

#ifndef ADSR_ENVELOPE_PROCESSOR_HPP
#define ADSR_ENVELOPE_PROCESSOR_HPP

#include "EnvelopeProcessor.hpp"
#include "Logger.hpp"
#include <atomic>
#include <span>
#include <algorithm>
#include <cmath>

namespace audio {

class AdsrEnvelopeProcessor : public EnvelopeProcessor {
public:
    enum class State { Idle, Attack, Decay, Sustain, Release };

    explicit AdsrEnvelopeProcessor(int sample_rate);

    void gate_on() override;
    void gate_off() override;
    void on_note_on(double /*frequency*/) override { gate_on(); }
    void on_note_off() override { gate_off(); }

    void inject_cv(std::string_view port_name, std::span<const float> cv) override;

    bool is_active()    const override { return state_ != State::Idle; }
    bool is_releasing() const override { return state_ == State::Release; }

    PortType output_port_type() const override { return PortType::PORT_CONTROL; }

    void reset() override { state_ = State::Idle; current_level_ = 0.0f; }

    bool apply_parameter(const std::string& name, float value) override;

    void set_attack_time(float seconds)  { attack_time_  = std::max(0.001f, seconds); update_rates(); }
    void set_decay_time(float seconds)   { decay_time_   = std::max(0.001f, seconds); update_rates(); }
    void set_sustain_level(float level)  { sustain_level_ = std::clamp(level, 0.0f, 1.0f); }
    void set_release_time(float seconds) { release_time_ = std::max(0.001f, seconds); update_rates(); }

    float get_level() const { return current_level_; }

protected:
    void do_pull(std::span<float> output, const VoiceContext* ctx = nullptr) override;

private:
    float process_sample();
    void  update_rates();

    static constexpr float kAttackTarget  =  1.001f;
    static constexpr float kReleaseTarget = -0.001f;

    int   sample_rate_;
    State state_;
    float current_level_;

    float attack_time_   = 0.01f;
    float decay_time_    = 0.1f;
    float sustain_level_ = 0.7f;
    float release_time_  = 0.2f;

    float attack_coeff_  = 0.0f;
    float decay_coeff_   = 0.0f;
    float release_coeff_ = 0.0f;

    bool ext_gate_high_ = false;
};

} // namespace audio

#endif // ADSR_ENVELOPE_PROCESSOR_HPP
