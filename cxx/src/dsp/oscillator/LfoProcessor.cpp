/**
 * @file LfoProcessor.cpp
 * @brief LFO do_pull, waveform calculation, and self-registration.
 */
#include "LfoProcessor.hpp"
#include "../../core/ModuleRegistry.hpp"
#include <cmath>
#include <algorithm>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace audio {

LfoProcessor::LfoProcessor(int sample_rate)
    : sample_rate_(sample_rate)
    , ramp_samples_(static_cast<int>(static_cast<float>(sample_rate) * kRampSeconds))
    , phase_(0.0)
    , waveform_(Waveform::Sine)
{
    declare_port({"rate_cv",     PORT_CONTROL, PortDirection::IN,  true});
    declare_port({"reset",       PORT_CONTROL, PortDirection::IN,  true});
    declare_port({"control_out", PORT_CONTROL, PortDirection::OUT, false});
    declare_port({"control_out_inv", PORT_CONTROL, PortDirection::OUT, false,
                  "Inverted copy of control_out; always exactly -1 x control_out. "
                  "Hardware: Roland M-150 OUT B. Eliminates a separate INVERTER node "
                  "when two destinations need counter-phase modulation."});

    declare_parameter({"rate",      "LFO Rate",      0.01f, 20.0f, 1.0f, true});
    declare_parameter({"intensity", "LFO Intensity", 0.0f,   1.0f, 1.0f});
    declare_parameter({"waveform",  "LFO Waveform",  0.0f,   3.0f, 0.0f});
    declare_parameter({"delay",     "LFO Delay",     0.0f,  10.0f, 0.0f, true});
    declare_parameter({"sync",      "Tempo Sync",    0.0f,   1.0f, 0.0f, false,
                        "0=off, 1=on. When on, 'rate' is ignored; LFO period derived from bpm+division."});
    declare_parameter({"division",  "Beat Division", 0.0f,  10.0f, 2.0f, false,
                        "0=whole 1=half 2=quarter 3=dotted_quarter 4=eighth 5=dotted_eighth "
                        "6=triplet_quarter 7=sixteenth 8=triplet_eighth 9=thirtysecond 10=sixtyfourth"});
}

bool LfoProcessor::apply_parameter(const std::string& name, float value) {
    if (name == "rate") {
        rate_.set_target(static_cast<float>(std::max(0.01f, value)), ramp_samples_);
        return true;
    }
    if (name == "intensity") {
        intensity_.set_target(std::clamp(value, 0.0f, 1.0f), ramp_samples_);
        return true;
    }
    if (name == "waveform") {
        int w = static_cast<int>(value);
        if (w >= 0 && w <= 3) { waveform_ = static_cast<Waveform>(w); return true; }
        return false;
    }
    if (name == "delay") {
        delay_time_.set_target(std::max(0.0f, value), ramp_samples_);
        delay_samples_remaining_ = static_cast<size_t>(delay_time_.get() * static_cast<float>(sample_rate_));
        return true;
    }
    if (name == "sync") {
        sync_ = (value != 0.0f);
        return true;
    }
    if (name == "division") {
        division_ = std::clamp(static_cast<int>(value), 0, kDivisionCount - 1);
        return true;
    }
    return false;
}

void LfoProcessor::reset() {
    phase_ = 0.0;
    intensity_.snap();
    rate_.snap();
    delay_time_.snap();
    delay_samples_remaining_ = static_cast<size_t>(delay_time_.get() * static_cast<float>(sample_rate_));
}

void LfoProcessor::do_pull(std::span<float> output, const VoiceContext* ctx) {
    // Tempo-sync: derive rate from bpm + division each block.
    if (sync_ && ctx) {
        const float period = beat_time_seconds(ctx->get_bpm(), division_);
        const float synced_rate = (period > 0.0f) ? (1.0f / period) : 0.01f;
        rate_.set_target(std::clamp(synced_rate, 0.01f, 20.0f), ramp_samples_);
    }

    const int n = static_cast<int>(output.size());
    rate_.advance(n);
    intensity_.advance(n);
    delay_time_.advance(n);

    if (delay_samples_remaining_ > 0) {
        const size_t consumed = std::min(delay_samples_remaining_, output.size());
        delay_samples_remaining_ -= consumed;
        std::fill(output.begin(), output.end(), 0.0f);
        return;
    }

    // Hard-sync: reset phase on positive edge of reset_cv port.
    if (reset_cv_in_ > 0.0f && prev_reset_ <= 0.0f) phase_ = 0.0;
    prev_reset_  = reset_cv_in_;
    reset_cv_in_ = 0.0f;

    // rate_cv adds linearly to LFO rate (clamped to valid range).
    const float effective_rate = std::clamp(rate_.get() + rate_cv_in_, 0.01f, 20.0f);
    rate_cv_in_ = 0.0f;

    const double frequency = static_cast<double>(effective_rate);
    const double phase_inc = frequency / static_cast<double>(sample_rate_);
    const size_t frames = output.size();

    const float lfo_val   = calculate_waveform();
    const float final_val = lfo_val * intensity_.get();
    std::fill(output.begin(), output.end(), final_val);

    phase_ = std::fmod(phase_ + phase_inc * static_cast<double>(frames), 1.0);
}

void LfoProcessor::do_pull(AudioBuffer& output, const VoiceContext* ctx) {
    do_pull(output.left, ctx);
    std::copy(output.left.begin(), output.left.end(), output.right.begin());
}

float LfoProcessor::calculate_waveform() const {
    switch (waveform_) {
        case Waveform::Sine:
            return static_cast<float>(std::sin(2.0 * M_PI * phase_));
        case Waveform::Triangle:
            return 2.0f * std::abs(2.0f * static_cast<float>(phase_) - 1.0f) - 1.0f;
        case Waveform::Square:
            return (phase_ < 0.5) ? 1.0f : -1.0f;
        case Waveform::Saw:
            return 2.0f * static_cast<float>(phase_) - 1.0f;
        default:
            return 0.0f;
    }
}

namespace {
[[maybe_unused]] const bool kRegistered = ModuleRegistry::instance().register_module(
    "LFO",
    "Low-frequency oscillator (sine/triangle/square/saw) with intensity smoothing",
    "Connect control_out → VCO.pitch_cv for vibrato, → VCF.cutoff_cv for filter wobble, "
    "or → VCA.gain_cv for tremolo. intensity is in semitones for pitch_cv. "
    "rate below 1 Hz for slow evolution; above 5 Hz for trill/tremolo effects.",
    [](int sr) { return std::make_unique<LfoProcessor>(sr); }
);
} // namespace

} // namespace audio
