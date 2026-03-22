/**
 * @file GateDelayProcessor.cpp
 * @brief Gate delay do_pull and self-registration.
 */
#include "GateDelayProcessor.hpp"
#include "../../core/ModuleRegistry.hpp"
#include <algorithm>

namespace audio {

GateDelayProcessor::GateDelayProcessor(int sample_rate)
    : sample_rate_(sample_rate)
{
    declare_port({"gate_in",   PORT_CONTROL, PortDirection::IN,  true,
                  "Lifecycle gate input — driven by VoiceContext note_on/note_off. "
                  "Not wirable via connections_."});
    declare_port({"gate_in_b", PORT_CONTROL, PortDirection::IN,  true,
                  "Secondary non-lifecycle gate input, OR'd with gate_in. "
                  "Wirable via connections_ to any PORT_CONTROL source "
                  "(e.g. LFO square wave, another GATE_DELAY output). "
                  "Hardware: Roland M-172 INPUT B."});
    declare_port({"gate_out",  PORT_CONTROL, PortDirection::OUT, true});

    declare_parameter({"delay_time", "Delay Time (s)", 0.0f, 6.0f, 0.0f, true,
                        "Time from input gate rising edge to gate_out rising edge. "
                        "Hardware M-172 range: 0.3 ms–6 s."});
    declare_parameter({"gate_time",  "Gate Time (s)",  0.0f, 6.0f, 0.0f, true,
                        "Output pulse width in seconds. 0 = mirror input gate duration "
                        "(gate_out stays high while input is held). >0 = fixed-length "
                        "pulse of gate_time seconds regardless of input hold duration. "
                        "Hardware: Roland M-172 GATE TIME control."});
}

bool GateDelayProcessor::apply_parameter(const std::string& name, float value) {
    if (name == "delay_time") {
        delay_time_.set_target(std::clamp(value, 0.0f, 6.0f), 0);
        return true;
    }
    if (name == "gate_time") {
        gate_time_.set_target(std::clamp(value, 0.0f, 6.0f), 0);
        return true;
    }
    return false;
}

void GateDelayProcessor::trigger() {
    const float dt = delay_time_.get();
    if (dt <= 0.0f) {
        const float gt = gate_time_.get();
        if (gt > 0.0f) {
            state_              = State::Pulsing;
            pulse_samples_left_ = static_cast<int>(gt * static_cast<float>(sample_rate_));
        } else {
            state_              = State::High;
            delay_samples_left_ = 0;
        }
    } else {
        state_              = State::Counting;
        delay_samples_left_ = static_cast<int>(dt * static_cast<float>(sample_rate_));
    }
}

void GateDelayProcessor::on_note_on(double /*frequency*/) {
    lifecycle_gate_high_ = true;
    trigger();
}

void GateDelayProcessor::on_note_off() {
    lifecycle_gate_high_ = false;
    // Only drop immediately if not in a fixed-duration pulse
    if (state_ == State::High) {
        state_              = State::Idle;
        delay_samples_left_ = 0;
    }
}

void GateDelayProcessor::do_pull(std::span<float> output, const VoiceContext*) {
    delay_time_.advance(static_cast<int>(output.size()));
    gate_time_.advance(static_cast<int>(output.size()));

    // Detect gate_in_b rising edge (positive edge: prev<=0, now>0).
    const float cur_b = gate_in_b_cv_;
    gate_in_b_cv_     = 0.0f;  // consume

    if (cur_b > 0.0f && prev_gate_in_b_ <= 0.0f) {
        trigger();
    }
    prev_gate_in_b_ = cur_b;

    for (auto& s : output) {
        switch (state_) {
            case State::Counting:
                s = 0.0f;
                if (--delay_samples_left_ <= 0) {
                    const float gt = gate_time_.get();
                    if (gt > 0.0f) {
                        state_              = State::Pulsing;
                        pulse_samples_left_ = static_cast<int>(
                            gt * static_cast<float>(sample_rate_));
                    } else {
                        state_ = lifecycle_gate_high_ ? State::High : State::Idle;
                    }
                }
                break;
            case State::High:
                s = 1.0f;
                break;
            case State::Pulsing:
                s = 1.0f;
                if (--pulse_samples_left_ <= 0) state_ = State::Idle;
                break;
            case State::Idle:
            default:
                s = 0.0f;
                break;
        }
    }
}

namespace {
[[maybe_unused]] const bool kRegistered = ModuleRegistry::instance().register_module(
    "GATE_DELAY",
    "Gate pulse shaper — delays and optionally reshapes a gate signal",
    "Connect gate_out → ADSR:gate_cv for a delayed secondary envelope. "
    "delay_time=0.3 lets the initial attack complete before firing. "
    "gate_time>0 fires a fixed-length pulse regardless of how long the note is held. "
    "gate_in_b accepts a second wirable gate source OR'd with the lifecycle gate "
    "(e.g. LFO square wave for repeating retriggered pulses). "
    "Hardware: Roland M-172 Gate Delay section.",
    [](int sr) { return std::make_unique<GateDelayProcessor>(sr); }
);
} // namespace

} // namespace audio
