/**
 * @file PhaserProcessor.cpp
 * @brief Phaser do_pull, all-pass helpers, and self-registration.
 */
#include "PhaserProcessor.hpp"
#include "../../core/ModuleRegistry.hpp"
#include <cmath>
#include <algorithm>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace audio {

PhaserProcessor::PhaserProcessor(int sample_rate)
    : sample_rate_(sample_rate)
    , ramp_samples_(static_cast<int>(static_cast<float>(sample_rate) * kRampSeconds))
{
    declare_port({"audio_in",  PORT_AUDIO, PortDirection::IN});
    declare_port({"audio_out", PORT_AUDIO, PortDirection::OUT});
    declare_parameter({"rate",      "LFO Rate (Hz)",     0.01f, 20.0f,  0.5f, true});
    declare_parameter({"depth",     "Depth (oct)",        0.0f,  4.0f,  1.5f});
    declare_parameter({"feedback",  "Feedback",          -0.95f, 0.95f, 0.7f});
    declare_parameter({"base_freq", "Base Freq (Hz)",    20.0f, 4000.0f, 400.0f});
    declare_parameter({"stages",    "Stages (4 or 8)",    4.0f,  8.0f,  4.0f});
    declare_parameter({"wet",       "Wet/Dry",            0.0f,  1.0f,  0.5f});
}

bool PhaserProcessor::apply_parameter(const std::string& name, float value) {
    if (name == "rate")      { rate_.set_target(std::max(0.01f, value), ramp_samples_);          return true; }
    if (name == "depth")     { depth_.set_target(std::clamp(value, 0.0f, 4.0f), ramp_samples_); return true; }
    if (name == "feedback")  { feedback_.set_target(std::clamp(value, -0.95f, 0.95f), ramp_samples_); return true; }
    if (name == "base_freq") { base_freq_.set_target(std::clamp(value, 20.0f, 4000.0f), ramp_samples_); return true; }
    if (name == "stages")    { stages_ = (value >= 6.5f) ? 8 : 4; return true; }
    if (name == "wet")       { wet_.set_target(std::clamp(value, 0.0f, 1.0f), ramp_samples_);   return true; }
    return false;
}

void PhaserProcessor::do_pull(AudioBuffer& buf, const VoiceContext*) {
    const int n = static_cast<int>(buf.frames());
    rate_.advance(n);
    depth_.advance(n);
    feedback_.advance(n);
    base_freq_.advance(n);
    wet_.advance(n);

    const float wet_val       = wet_.get();
    const float dry           = 1.0f - wet_val;
    const float rate_val      = rate_.get();
    const float depth_val     = depth_.get();
    const float feedback_val  = feedback_.get();
    const float base_freq_val = base_freq_.get();
    const float phase_inc     = rate_val / static_cast<float>(sample_rate_);
    const size_t frames       = buf.frames();

    for (size_t f = 0; f < frames; ++f) {
        phase_l_ += phase_inc;
        if (phase_l_ >= 1.0) phase_l_ -= 1.0;
        phase_r_ += phase_inc;
        if (phase_r_ >= 1.0) phase_r_ -= 1.0;

        const float lfo_l = static_cast<float>(std::sin(2.0 * M_PI * phase_l_));
        const float lfo_r = static_cast<float>(std::sin(2.0 * M_PI * phase_r_));
        const float fc_l  = base_freq_val * std::pow(2.0f, depth_val * lfo_l);
        const float fc_r  = base_freq_val * std::pow(2.0f, depth_val * lfo_r);

        const float a1_l = allpass_coeff(std::clamp(fc_l, 1.0f, static_cast<float>(sample_rate_) * 0.49f));
        const float a1_r = allpass_coeff(std::clamp(fc_r, 1.0f, static_cast<float>(sample_rate_) * 0.49f));

        float inL = buf.left[f] + fb_l_ * feedback_val;
        float outL = process_cascade(inL, a1_l, xl_prev_, yl_prev_);
        fb_l_ = outL;

        float inR = buf.right[f] + fb_r_ * feedback_val;
        float outR = process_cascade(inR, a1_r, xr_prev_, yr_prev_);
        fb_r_ = outR;

        buf.left[f]  = outL * wet_val + buf.left[f]  * dry;
        buf.right[f] = outR * wet_val + buf.right[f] * dry;
    }
}

float PhaserProcessor::allpass_coeff(float fc) const {
    const float t = std::tan(static_cast<float>(M_PI) * fc / static_cast<float>(sample_rate_));
    return (t - 1.0f) / (t + 1.0f);
}

float PhaserProcessor::process_cascade(float in, float a1,
                                        float x_prev[kMaxStages],
                                        float y_prev[kMaxStages]) {
    float s = in;
    for (int i = 0; i < stages_; ++i) {
        const float y = a1 * (s - y_prev[i]) + x_prev[i];
        x_prev[i] = s;
        y_prev[i] = y;
        s = y;
    }
    return s;
}

namespace {
[[maybe_unused]] const bool kRegistered = ModuleRegistry::instance().register_module(
    "PHASER",
    "Stereo quadrature all-pass phaser (4/8-stage) with LFO sweep",
    "Connect audio_out for classic phaser sweep. rate=0.3..2 Hz for slow sweep. "
    "depth=1.5 octaves gives wide notch sweep. feedback=0.7 for deep notches. "
    "stages=8 for denser, more complex phasing.",
    [](int sr) { return std::make_unique<PhaserProcessor>(sr); }
);
} // namespace

} // namespace audio
