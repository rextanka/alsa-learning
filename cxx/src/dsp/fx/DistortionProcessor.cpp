/**
 * @file DistortionProcessor.cpp
 * @brief Distortion do_pull, waveshaper, and self-registration.
 */
#include "DistortionProcessor.hpp"
#include "../../core/ModuleRegistry.hpp"
#include <cmath>
#include <algorithm>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace audio {

DistortionProcessor::DistortionProcessor(int sample_rate)
    : ramp_samples_(static_cast<int>(static_cast<float>(sample_rate) * kRampSeconds))
{
    declare_port({"audio_in",  PORT_AUDIO, PortDirection::IN});
    declare_port({"audio_out", PORT_AUDIO, PortDirection::OUT});
    declare_parameter({"drive",     "Drive",     1.0f, 40.0f, 8.0f});
    declare_parameter({"character", "Character", 0.0f,  1.0f, 0.3f});

    // Anti-aliasing LP at original Nyquist, running at 4× rate.
    // g = exp(-2π·(fs/2) / (4·fs)) = exp(-π/4) ≈ 0.456
    g_aa_ = std::exp(-static_cast<float>(M_PI) / static_cast<float>(kOversample));
}

bool DistortionProcessor::apply_parameter(const std::string& name, float value) {
    if (name == "drive") {
        drive_.set_target(std::clamp(value, 1.0f, 40.0f), ramp_samples_);
        return true;
    }
    if (name == "character") {
        character_.set_target(std::clamp(value, 0.0f, 1.0f), ramp_samples_);
        return true;
    }
    return false;
}

void DistortionProcessor::do_pull(std::span<float> output, const VoiceContext*) {
    const int n = static_cast<int>(output.size());
    drive_.advance(n);
    character_.advance(n);

    const float drv = drive_.get();
    const float chr = character_.get();

    for (auto& s : output) {
        // 4× linear interpolation upsampling, distort each sub-sample,
        // run 2-pole IIR AA filter, decimate by taking the last result.
        float result = 0.0f;
        for (int k = 0; k < kOversample; ++k) {
            const float t  = static_cast<float>(k + 1) / static_cast<float>(kOversample);
            const float up = x_prev_ + t * (s - x_prev_);

            const float ds = waveshape(up * drv, chr);

            aa_[0] += (1.0f - g_aa_) * (ds     - aa_[0]);
            aa_[1] += (1.0f - g_aa_) * (aa_[0] - aa_[1]);
            result  = aa_[1];
        }
        x_prev_ = s;
        s = result;
    }
}

float DistortionProcessor::waveshape(float x, float character) noexcept {
    const float soft = std::tanh(x);
    const float hard = (x >= 0.0f)
        ? std::tanh(x * 4.0f) * 0.5f
        : std::tanh(x);
    return soft + character * (hard - soft);
}

namespace {
[[maybe_unused]] const bool kRegistered = ModuleRegistry::instance().register_module(
    "DISTORTION",
    "Guitar/pedal distortion — 4× oversampled tanh/asymmetric waveshaper",
    "Place after any VCO or AUDIO_MIXER. drive=8 for warm overdrive, drive=30 for heavy. "
    "character=0 for symmetric tanh (tube-like odd harmonics), =1 for asymmetric clip "
    "(adds even harmonics for spitty transistor tone). Precede with HIGH_PASS_FILTER "
    "to thin out bass before clipping.",
    [](int sr) { return std::make_unique<DistortionProcessor>(sr); }
);
} // namespace

} // namespace audio
