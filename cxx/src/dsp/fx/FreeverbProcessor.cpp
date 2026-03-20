/**
 * @file FreeverbProcessor.cpp
 * @brief Freeverb reverb do_pull, filter helpers, and self-registration.
 */
#include "FreeverbProcessor.hpp"
#include "../../core/ModuleRegistry.hpp"
#include <algorithm>
#include <vector>

namespace audio {

FreeverbProcessor::FreeverbProcessor(int sample_rate) : sample_rate_(sample_rate) {
    ramp_samples_ = static_cast<int>(static_cast<float>(sample_rate) * kRampSeconds);
    declare_port({"audio_in",  PORT_AUDIO, PortDirection::IN});
    declare_port({"audio_out", PORT_AUDIO, PortDirection::OUT});
    declare_parameter({"room_size", "Room Size",  0.0f, 1.0f, 0.5f});
    declare_parameter({"damping",   "Damping",    0.0f, 1.0f, 0.5f});
    declare_parameter({"width",     "Width",      0.0f, 1.0f, 1.0f});
    declare_parameter({"wet",       "Wet/Dry",    0.0f, 1.0f, 0.33f});
    init_filters();
}

bool FreeverbProcessor::apply_parameter(const std::string& name, float value) {
    if (name == "room_size") { room_size_.set_target(std::clamp(value, 0.0f, 1.0f), ramp_samples_); update_params(); return true; }
    if (name == "damping")   { damping_.set_target(std::clamp(value, 0.0f, 1.0f), ramp_samples_); update_params(); return true; }
    if (name == "width")     { width_.set_target(std::clamp(value, 0.0f, 1.0f), ramp_samples_); return true; }
    if (name == "wet")       { wet_.set_target(std::clamp(value, 0.0f, 1.0f), ramp_samples_); return true; }
    return false;
}

void FreeverbProcessor::do_pull(std::span<float> output, const VoiceContext* ctx) {
    std::vector<float> l(output.size()), r(output.size());
    std::copy(output.begin(), output.end(), l.begin());
    std::copy(output.begin(), output.end(), r.begin());
    AudioBuffer buf { std::span<float>(l), std::span<float>(r) };
    do_pull(buf, ctx);
    for (size_t i = 0; i < output.size(); ++i)
        output[i] = (l[i] + r[i]) * 0.5f;
}

void FreeverbProcessor::do_pull(AudioBuffer& buf, const VoiceContext*) {
    const int n = static_cast<int>(buf.frames());
    room_size_.advance(n);
    damping_.advance(n);
    width_.advance(n);
    wet_.advance(n);

    if (room_size_.is_ramping() || damping_.is_ramping()) {
        update_params();
    }

    const float wet_val   = wet_.get();
    const float width_val = width_.get();
    const float wet1 = wet_val * (width_val * 0.5f + 0.5f);
    const float wet2 = wet_val * (0.5f - width_val * 0.5f);
    const float dry  = 1.0f - wet_val;
    const size_t frames = buf.frames();

    for (size_t i = 0; i < frames; ++i) {
        const float inL  = buf.left[i];
        const float inR  = buf.right[i];
        const float mono = (inL + inR) * kInputGain;

        float outL = 0.0f, outR = 0.0f;
        for (int c = 0; c < N_COMB; ++c) {
            outL += comb_l_[c].process(mono);
            outR += comb_r_[c].process(mono);
        }
        for (int a = 0; a < N_AP; ++a) {
            outL = ap_l_[a].process(outL);
            outR = ap_r_[a].process(outR);
        }

        buf.left[i]  = outL * wet1 + outR * wet2 + inL * dry;
        buf.right[i] = outR * wet1 + outL * wet2 + inR * dry;
    }
}

void FreeverbProcessor::init_filters() {
    const float scale = static_cast<float>(sample_rate_) / 44100.0f;
    for (int i = 0; i < N_COMB; ++i) {
        comb_l_[i].init(std::max(static_cast<int>(kCombBase[i] * scale + 0.5f), 4));
        comb_r_[i].init(std::max(static_cast<int>((kCombBase[i] + kStereoSpread) * scale + 0.5f), 4));
    }
    for (int i = 0; i < N_AP; ++i) {
        int len = std::max(static_cast<int>(kApBase[i] * scale + 0.5f), 4);
        ap_l_[i].init(len);
        ap_r_[i].init(len);
    }
    update_params();
}

void FreeverbProcessor::update_params() {
    const float fb   = room_size_.get() * 0.28f + 0.70f;  // 0.70..0.98
    const float damp = damping_.get();
    for (int i = 0; i < N_COMB; ++i) {
        comb_l_[i].set_feedback(fb);
        comb_r_[i].set_feedback(fb);
        comb_l_[i].set_damp(damp);
        comb_r_[i].set_damp(damp);
    }
}

namespace {
[[maybe_unused]] const bool kRegistered = ModuleRegistry::instance().register_module(
    "FREEVERB",
    "Schroeder/Freeverb plate reverb — 8 comb + 4 all-pass, stereo",
    "Insert after any instrument. room_size=0.8 for large hall, =0.3 for tight room. "
    "damping=0.7 absorbs high frequencies (realistic rooms). "
    "wet=0.25 for subtle ambience; wet=0.8 for washed-out pad textures.",
    [](int sr) { return std::make_unique<FreeverbProcessor>(sr); }
);
} // namespace

} // namespace audio
