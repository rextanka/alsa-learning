/**
 * @file EchoDelayProcessor.cpp
 * @brief Modulated echo delay do_pull and self-registration.
 */
#include "EchoDelayProcessor.hpp"
#include "../../core/ModuleRegistry.hpp"
#include <cmath>
#include <algorithm>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace audio {

EchoDelayProcessor::EchoDelayProcessor(int sample_rate)
    : sample_rate_(sample_rate)
    , ramp_samples_(static_cast<int>(static_cast<float>(sample_rate) * kRampSeconds))
    , delay_(sample_rate, 5.0f)
    , lfo_phase_(0.0)
{
    delay_.set_delay_time(delay_time_.get());
    delay_.set_feedback(feedback_.get());
    delay_.set_mix(mix_.get());

    declare_port({"audio_in",  PORT_AUDIO, PortDirection::IN});
    declare_port({"audio_out", PORT_AUDIO, PortDirection::OUT});

    declare_parameter({"time",          "Delay Time",      0.0f,  5.0f, 0.25f, true});
    declare_parameter({"feedback",      "Feedback",        0.0f,  0.95f, 0.3f});
    declare_parameter({"mix",           "Wet/Dry Mix",     0.0f,  1.0f,  0.5f});
    declare_parameter({"mod_rate",      "Mod Rate (Hz)",   0.0f, 20.0f,  0.0f});
    declare_parameter({"mod_intensity", "Mod Intensity",   0.0f,  1.0f,  0.0f});
}

bool EchoDelayProcessor::apply_parameter(const std::string& name, float value) {
    if (name == "time") {
        // Snap immediately — delay time is a patch-configuration value set before
        // audio starts. A gradual ramp would use the wrong initial delay time.
        delay_time_.set_target(std::clamp(value, 0.001f, 5.0f), 0);
        delay_.set_delay_time(delay_time_.get());
        return true;
    }
    if (name == "feedback") {
        feedback_.set_target(std::clamp(value, 0.0f, 0.95f), ramp_samples_);
        delay_.set_feedback(feedback_.get());
        return true;
    }
    if (name == "mix") {
        mix_.set_target(std::clamp(value, 0.0f, 1.0f), ramp_samples_);
        delay_.set_mix(mix_.get());
        return true;
    }
    if (name == "mod_rate") {
        mod_rate_.set_target(std::max(0.0f, value), ramp_samples_);
        return true;
    }
    if (name == "mod_intensity") {
        mod_intensity_.set_target(std::clamp(value, 0.0f, 1.0f), ramp_samples_);
        return true;
    }
    return false;
}

void EchoDelayProcessor::do_pull(std::span<float> output, const VoiceContext*) {
    const int n = static_cast<int>(output.size());
    delay_time_.advance(n);
    feedback_.advance(n);
    mix_.advance(n);
    mod_rate_.advance(n);
    mod_intensity_.advance(n);

    if (feedback_.is_ramping()) delay_.set_feedback(feedback_.get());
    if (mix_.is_ramping())      delay_.set_mix(mix_.get());

    const float dt_val  = delay_time_.get();
    const float mr_val  = mod_rate_.get();
    const float mi_val  = mod_intensity_.get();
    const double phase_inc = (sample_rate_ > 0 && mr_val > 0.0f)
        ? static_cast<double>(mr_val) / static_cast<double>(sample_rate_)
        : 0.0;

    for (auto& sample : output) {
        if (mr_val > 0.0f && mi_val > 0.0f) {
            float lfo = static_cast<float>(std::sin(2.0 * M_PI * lfo_phase_));
            float mod = dt_val * mi_val * lfo;
            delay_.set_delay_time(std::max(0.001f, dt_val + mod));
            lfo_phase_ += phase_inc;
            if (lfo_phase_ >= 1.0) lfo_phase_ -= 1.0;
        }
        sample = delay_.process_sample(sample);
    }

    // Restore nominal delay time after block (so feedback path is consistent)
    if (mr_val > 0.0f && mi_val > 0.0f) {
        delay_.set_delay_time(dt_val);
    }
}

namespace {
[[maybe_unused]] const bool kRegistered = ModuleRegistry::instance().register_module(
    "ECHO_DELAY",
    "Modulated delay line with LFO depth control for echo and chorus shimmer",
    "Place after any source. Set time=0.3, feedback=0.4 for slapback. "
    "Enable mod_rate + mod_intensity for BBD-style chorus shimmer. "
    "Higher feedback approaching 0.95 gives long, self-oscillating echoes.",
    [](int sr) { return std::make_unique<EchoDelayProcessor>(sr); }
);
} // namespace

} // namespace audio
