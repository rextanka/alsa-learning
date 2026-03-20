/**
 * @file JunoChorus.cpp
 * @brief Juno-60 BBD chorus do_pull and self-registration.
 */
#include "JunoChorus.hpp"
#include "../../core/ModuleRegistry.hpp"
#include <cmath>
#include <algorithm>
#include <vector>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace audio {

JunoChorus::JunoChorus(int sample_rate)
    : sample_rate_(sample_rate)
    , ramp_samples_(static_cast<int>(static_cast<float>(sample_rate) * kRampSeconds))
    , delay_l_(sample_rate, 0.01f)
    , delay_r_(sample_rate, 0.01f)
{
    delay_l_.set_feedback(0.0f);
    delay_r_.set_feedback(0.0f);
    delay_l_.set_mix(0.5f);
    delay_r_.set_mix(0.5f);

    declare_port({"audio_in",  PORT_AUDIO, PortDirection::IN});
    declare_port({"audio_out", PORT_AUDIO, PortDirection::OUT});

    declare_parameter({"mode",  "Chorus Mode",  0.0f,  3.0f, 0.0f});
    declare_parameter({"rate",  "Chorus Rate",  0.1f, 10.0f, 0.5f});
    declare_parameter({"depth", "Chorus Depth", 0.0f,  1.0f, 0.5f});
}

bool JunoChorus::apply_parameter(const std::string& name, float value) {
    if (name == "mode") {
        set_mode(static_cast<Mode>(static_cast<int>(std::round(value))));
        return true;
    }
    if (name == "rate") {
        lfo_rate_.set_target(static_cast<float>(value), ramp_samples_);
        return true;
    }
    if (name == "depth") {
        lfo_depth_.set_target(static_cast<float>(value) * 0.003f, ramp_samples_);
        return true;
    }
    return false;
}

void JunoChorus::set_mode(Mode mode) {
    mode_ = mode;
    switch (mode) {
        case Mode::I:    lfo_rate_.set_target(0.4f, 0); lfo_depth_.set_target(0.002f, 0); break;
        case Mode::II:   lfo_rate_.set_target(0.6f, 0); lfo_depth_.set_target(0.002f, 0); break;
        case Mode::I_II: lfo_rate_.set_target(1.0f, 0); lfo_depth_.set_target(0.003f, 0); break;
        default:         lfo_rate_.set_target(0.0f, 0); lfo_depth_.set_target(0.0f,   0); break;
    }
}

void JunoChorus::do_pull(AudioBuffer& output, const VoiceContext*) {
    if (mode_ == Mode::Off) return;

    const int n = static_cast<int>(output.frames());
    lfo_rate_.advance(n);
    lfo_depth_.advance(n);

    const size_t frames    = output.frames();
    const double phase_inc = static_cast<double>(lfo_rate_.get()) / static_cast<double>(sample_rate_);
    const double depth_val = static_cast<double>(lfo_depth_.get());

    for (size_t i = 0; i < frames; ++i) {
        double mod = std::sin(2.0 * M_PI * lfo_phase_);
        lfo_phase_ += phase_inc;
        if (lfo_phase_ >= 1.0) lfo_phase_ -= 1.0;

        float delay_ms_l = static_cast<float>(0.0035 + mod * depth_val);
        float delay_ms_r = static_cast<float>(0.0035 - mod * depth_val);

        delay_l_.set_delay_time(delay_ms_l);
        delay_r_.set_delay_time(delay_ms_r);

        output.left[i]  = delay_l_.process_sample(output.left[i]);
        output.right[i] = delay_r_.process_sample(output.right[i]);
    }
}

void JunoChorus::do_pull(std::span<float> output, const VoiceContext*) {
    std::vector<float> l(output.size()), r(output.size());
    AudioBuffer temp { std::span<float>(l), std::span<float>(r) };
    std::copy(output.begin(), output.end(), l.begin());
    std::copy(output.begin(), output.end(), r.begin());

    do_pull(temp);

    for (size_t i = 0; i < output.size(); ++i)
        output[i] = (l[i] + r[i]) * 0.5f;
}

namespace {
[[maybe_unused]] const bool kRegistered = ModuleRegistry::instance().register_module(
    "JUNO_CHORUS",
    "Juno-60 BBD stereo chorus — dual modulated delay lines with quadrature LFO",
    "Place on any pad or string voice. mode=1 (I) for classic slow shimmer, "
    "mode=2 (II) for faster modulation, mode=3 (I+II) for rich double chorus. "
    "depth=0.5 gives authentic Juno width; higher values add more pitch wobble.",
    [](int sr) { return std::make_unique<JunoChorus>(sr); }
);
} // namespace

} // namespace audio
