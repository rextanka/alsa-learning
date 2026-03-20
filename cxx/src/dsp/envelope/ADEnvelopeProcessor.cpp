/**
 * @file ADEnvelopeProcessor.cpp
 * @brief AD envelope state machine and self-registration.
 */
#include "ADEnvelopeProcessor.hpp"
#include "../../core/ModuleRegistry.hpp"
#include <cmath>

namespace audio {

ADEnvelopeProcessor::ADEnvelopeProcessor(int sample_rate)
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

void ADEnvelopeProcessor::gate_on() {
    state_ = State::Attack;
    update_rates();
}

bool ADEnvelopeProcessor::apply_parameter(const std::string& name, float value) {
    if (name == "attack") { attack_time_ = std::max(0.001f, value); update_rates(); return true; }
    if (name == "decay")  { decay_time_  = std::max(0.001f, value); update_rates(); return true; }
    return false;
}

void ADEnvelopeProcessor::do_pull(std::span<float> output, const VoiceContext* /*ctx*/) {
    for (auto& sample : output) sample = process_sample();
}

float ADEnvelopeProcessor::process_sample() {
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

void ADEnvelopeProcessor::update_rates() {
    static constexpr float kLog9 = 2.197224577f;
    const float sr = static_cast<float>(sample_rate_);
    attack_coeff_ = std::exp(-kLog9 / (attack_time_ * sr));
    decay_coeff_  = std::exp(-kLog9 / (decay_time_  * sr));
}

namespace {
[[maybe_unused]] const bool kRegistered = ModuleRegistry::instance().register_module(
    "AD_ENVELOPE",
    "Attack-Decay envelope for percussive sounds — ignores gate_off, completes Decay regardless",
    "Use for drums and one-shot sounds that must complete their decay "
    "even if note_off fires early. attack=0.001s for sharp transients. "
    "Unlike ADSR, AD ignores note_off — the decay always runs to completion.",
    [](int sr) { return std::make_unique<ADEnvelopeProcessor>(sr); }
);
} // namespace

} // namespace audio
