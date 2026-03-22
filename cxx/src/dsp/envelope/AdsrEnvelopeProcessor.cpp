/**
 * @file AdsrEnvelopeProcessor.cpp
 * @brief ADSR envelope state machine and self-registration.
 */
#include "AdsrEnvelopeProcessor.hpp"
#include "../../core/ModuleRegistry.hpp"

namespace audio {

AdsrEnvelopeProcessor::AdsrEnvelopeProcessor(int sample_rate)
    : sample_rate_(sample_rate)
    , state_(State::Idle)
    , current_level_(0.0f)
{
    update_rates();

    declare_port({"gate_in",      PORT_CONTROL, PortDirection::IN,  true});  // lifecycle (Voice callback)
    declare_port({"trigger_in",   PORT_CONTROL, PortDirection::IN,  true});  // lifecycle (Voice callback)
    declare_port({"envelope_out", PORT_CONTROL, PortDirection::OUT, true});
    declare_port({"gate_cv",      PORT_CONTROL, PortDirection::IN,  true});  // wirable: rising=attack, falling=release
    declare_port({"trig_cv",      PORT_CONTROL, PortDirection::IN,  true});  // wirable: rising=attack only (retrigger)

    declare_parameter({"attack",  "Attack Time",   0.0f, 10.0f, 0.01f, true});
    declare_parameter({"decay",   "Decay Time",    0.0f, 10.0f, 0.1f,  true});
    declare_parameter({"sustain", "Sustain Level", 0.0f,  1.0f, 0.7f});
    declare_parameter({"release", "Release Time",  0.0f, 10.0f, 0.2f,  true});
}

void AdsrEnvelopeProcessor::gate_on() {
    AudioLogger::instance().log_event("ADSR", 1.0f);
    state_ = State::Attack;
    update_rates();
}

void AdsrEnvelopeProcessor::gate_off() {
    if (state_ != State::Idle) {
        state_ = State::Release;
        update_rates();
    }
}

void AdsrEnvelopeProcessor::inject_cv(std::string_view port_name, std::span<const float> cv) {
    if (cv.empty()) return;
    if (port_name == "gate_cv") {
        // Sustained gate: rising edge → attack, falling edge → release.
        for (float s : cv) {
            const bool high = (s > 0.5f);
            if (high && !gate_cv_high_) gate_on();
            else if (!high && gate_cv_high_) gate_off();
            gate_cv_high_ = high;
        }
    } else if (port_name == "trig_cv") {
        // Trigger: rising edge → attack only (no release on falling edge).
        // Enables retrigger without releasing a held gate (Roland "GATE+TRIG" pattern).
        for (float s : cv) {
            const bool high = (s > 0.5f);
            if (high && !trig_cv_high_) gate_on();
            trig_cv_high_ = high;
        }
    }
}

bool AdsrEnvelopeProcessor::apply_parameter(const std::string& name, float value) {
    if (name == "attack")  { set_attack_time(value);   return true; }
    if (name == "decay")   { set_decay_time(value);    return true; }
    if (name == "sustain") { set_sustain_level(value); return true; }
    if (name == "release") { set_release_time(value);  return true; }
    return false;
}

void AdsrEnvelopeProcessor::do_pull(std::span<float> output, const VoiceContext* /*ctx*/) {
    for (auto& sample : output) sample = process_sample();
}

float AdsrEnvelopeProcessor::process_sample() {
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

void AdsrEnvelopeProcessor::update_rates() {
    static constexpr float kLog9 = 2.197224577f;
    const float sr = static_cast<float>(sample_rate_);
    attack_coeff_  = std::exp(-kLog9 / (attack_time_  * sr));
    decay_coeff_   = std::exp(-kLog9 / (decay_time_   * sr));
    release_coeff_ = std::exp(-kLog9 / (release_time_ * sr));
}

namespace {
[[maybe_unused]] const bool kRegistered = ModuleRegistry::instance().register_module(
    "ADSR_ENVELOPE",
    "4-stage ADSR envelope generator (exponential IIR curves)",
    "Connect envelope_out → VCA.gain_cv for amplitude shaping. "
    "Also route to filter cutoff_cv for brightness sweep. "
    "sustain=0 for percussive sounds; release > 0 for legato tails. "
    "attack=0.001 for immediate percussive onset.",
    [](int sr) { return std::make_unique<AdsrEnvelopeProcessor>(sr); }
);
} // namespace

} // namespace audio
