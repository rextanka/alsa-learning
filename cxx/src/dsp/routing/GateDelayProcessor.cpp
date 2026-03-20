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
    declare_port({"gate_out", PORT_CONTROL, PortDirection::OUT, true});
    declare_parameter({"delay_time", "Delay Time (s)", 0.0f, 2.0f, 0.0f, true});
}

bool GateDelayProcessor::apply_parameter(const std::string& name, float value) {
    if (name == "delay_time") {
        // Snap immediately — controls gate-level logic, not audio samples.
        delay_time_.set_target(std::clamp(value, 0.0f, 2.0f), 0);
        return true;
    }
    return false;
}

void GateDelayProcessor::on_note_on(double /*frequency*/) {
    if (delay_time_.get() <= 0.0f) {
        state_        = State::High;
        samples_left_ = 0;
    } else {
        state_        = State::Counting;
        samples_left_ = static_cast<int>(delay_time_.get() * static_cast<float>(sample_rate_));
    }
}

void GateDelayProcessor::on_note_off() {
    state_        = State::Idle;
    samples_left_ = 0;
}

void GateDelayProcessor::do_pull(std::span<float> output, const VoiceContext*) {
    delay_time_.advance(static_cast<int>(output.size()));
    for (auto& s : output) {
        switch (state_) {
            case State::Counting:
                s = 0.0f;
                if (--samples_left_ <= 0) state_ = State::High;
                break;
            case State::High:
                s = 1.0f;
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
    "Gate pulse shaper — fires a gate signal delay_time seconds after note_on",
    "Connect GATE_DELAY:gate_out → ADSR2:ext_gate_in for a delayed secondary envelope. "
    "delay_time=0.3 lets the initial attack complete before the gate fires. "
    "If the note ends before delay_time elapses, gate_out never fires.",
    [](int sr) { return std::make_unique<GateDelayProcessor>(sr); }
);
} // namespace

} // namespace audio
