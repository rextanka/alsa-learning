/**
 * @file FdnReverbProcessor.cpp
 * @brief FDN reverb do_pull, buffer helpers, and self-registration.
 */
#include "FdnReverbProcessor.hpp"
#include "../../core/ModuleRegistry.hpp"
#include <cmath>
#include <algorithm>
#include <vector>

namespace audio {

FdnReverbProcessor::FdnReverbProcessor(int sample_rate, float max_room_size)
    : sample_rate_(sample_rate)
    , ramp_samples_(static_cast<int>(static_cast<float>(sample_rate) * kRampSeconds))
    , max_room_size_(std::clamp(max_room_size, 0.05f, 1.0f))
{
    declare_port({"audio_in",  PORT_AUDIO, PortDirection::IN});
    declare_port({"audio_out", PORT_AUDIO, PortDirection::OUT});
    declare_parameter({"decay",     "Decay (T60 s)",  0.1f, 30.0f,  2.0f, true});
    declare_parameter({"room_size", "Room Size",      0.0f,  1.0f,  0.5f});
    declare_parameter({"damping",   "Damping",        0.0f,  1.0f,  0.3f});
    declare_parameter({"width",     "Stereo Width",   0.0f,  1.0f,  1.0f});
    declare_parameter({"wet",       "Wet/Dry",        0.0f,  1.0f,  0.3f});
    alloc_buffers();
    update_delay_lengths();
    update_gains();
}

bool FdnReverbProcessor::apply_parameter(const std::string& name, float value) {
    if (name == "decay")     { decay_.set_target(std::max(0.1f, value), ramp_samples_);                     update_gains(); return true; }
    if (name == "room_size") { room_size_ = std::clamp(value, 0.0f, max_room_size_); update_delay_lengths(); update_gains(); return true; }
    if (name == "damping")   { damping_.set_target(std::clamp(value, 0.0f, 1.0f), ramp_samples_);          update_gains(); return true; }
    if (name == "width")     { width_.set_target(std::clamp(value, 0.0f, 1.0f), ramp_samples_);            return true; }
    if (name == "wet")       { wet_.set_target(std::clamp(value, 0.0f, 1.0f), ramp_samples_);              return true; }
    return false;
}

void FdnReverbProcessor::do_pull(std::span<float> output, const VoiceContext* ctx) {
    std::vector<float> l(output.size()), r(output.size());
    std::copy(output.begin(), output.end(), l.begin());
    std::copy(output.begin(), output.end(), r.begin());
    AudioBuffer buf { std::span<float>(l), std::span<float>(r) };
    do_pull(buf, ctx);
    for (size_t i = 0; i < output.size(); ++i)
        output[i] = (l[i] + r[i]) * 0.5f;
}

void FdnReverbProcessor::do_pull(AudioBuffer& buf, const VoiceContext*) {
    const int n = static_cast<int>(buf.frames());
    decay_.advance(n);
    damping_.advance(n);
    width_.advance(n);
    wet_.advance(n);
    if (decay_.is_ramping() || damping_.is_ramping()) {
        update_gains();
    }

    const float wet_val   = wet_.get();
    const float dry       = 1.0f - wet_val;
    const float out_gain  = wet_val * kOutGain;
    const float width_val = width_.get();
    const size_t frames   = buf.frames();

    for (size_t f = 0; f < frames; ++f) {
        const float inL     = buf.left[f];
        const float inR     = buf.right[f];
        const float in_mono = (inL + inR) * kInGain;

        // Read each delay line
        float x[N];
        for (int i = 0; i < N; ++i) {
            const int buf_size = static_cast<int>(d_state_[i].buf.size());
            const int read_pos = ((d_state_[i].write_pos - delay_len_[i]) % buf_size + buf_size) % buf_size;
            x[i] = d_state_[i].buf[read_pos];
        }

        // 1-pole lowpass (frequency-dependent damping)
        for (int i = 0; i < N; ++i) {
            d_state_[i].lp_state = lp_coeff_ * x[i]
                                 + (1.0f - lp_coeff_) * d_state_[i].lp_state;
            x[i] = d_state_[i].lp_state;
        }

        // Per-line T60 feedback gain
        for (int i = 0; i < N; ++i) x[i] *= g_[i];

        // Householder feedback matrix: y[i] = x[i] - (2/N) * sum(x)
        float sum = 0.0f;
        for (int i = 0; i < N; ++i) sum += x[i];
        const float fb_offset = sum * (2.0f / N);
        for (int i = 0; i < N; ++i) x[i] -= fb_offset;

        // Write back (+ input injection)
        for (int i = 0; i < N; ++i) {
            d_state_[i].buf[d_state_[i].write_pos] = x[i] + in_mono;
            if (++d_state_[i].write_pos >= static_cast<int>(d_state_[i].buf.size()))
                d_state_[i].write_pos = 0;
        }

        // Stereo extraction: even→L, odd→R
        float outL = 0.0f, outR = 0.0f;
        for (int i = 0; i < N; i += 2) outL += x[i];
        for (int i = 1; i < N; i += 2) outR += x[i];

        // Width blend
        const float mono_mix = (outL + outR) * 0.5f;
        outL = mono_mix + (outL - mono_mix) * width_val;
        outR = mono_mix + (outR - mono_mix) * width_val;

        buf.left[f]  = outL * out_gain + inL * dry;
        buf.right[f] = outR * out_gain + inR * dry;
    }
}

void FdnReverbProcessor::alloc_buffers() {
    const float sr          = static_cast<float>(sample_rate_);
    const float alloc_scale = 0.5f + max_room_size_ * 1.0f;
    for (int i = 0; i < N; ++i) {
        const int max_len = static_cast<int>(kBaseDelays_ms[i] * 0.001f * sr * alloc_scale) + 2;
        d_state_[i].buf.assign(max_len, 0.0f);
        d_state_[i].write_pos = 0;
        d_state_[i].lp_state  = 0.0f;
    }
}

void FdnReverbProcessor::update_delay_lengths() {
    const float sr         = static_cast<float>(sample_rate_);
    const float room_scale = 0.5f + room_size_ * 1.0f;
    for (int i = 0; i < N; ++i) {
        const int max_len = static_cast<int>(d_state_[i].buf.size());
        delay_len_[i] = std::clamp(
            static_cast<int>(kBaseDelays_ms[i] * 0.001f * sr * room_scale),
            8, max_len - 1
        );
    }
}

void FdnReverbProcessor::update_gains() {
    const float sr    = static_cast<float>(sample_rate_);
    const float decay = std::max(decay_.get(), 0.01f);
    for (int i = 0; i < N; ++i) {
        g_[i] = std::pow(10.0f, -3.0f * static_cast<float>(delay_len_[i]) / (decay * sr));
    }
    lp_coeff_ = 1.0f - damping_.get() * 0.85f;
}

namespace {
[[maybe_unused]] const bool kRegistered = ModuleRegistry::instance().register_module(
    "FDN_REVERB",
    "Feedback Delay Network reverb — Householder 8×8 matrix, exact T60 decay",
    "Use for dense, controllable reverb tails. decay=2.0 for medium hall. "
    "room_size scales delay lengths (0=small, 1=large). "
    "damping=0.6 for realistic HF absorption. Connect audio_out → VCA for gated reverb.",
    [](int sr) { return std::make_unique<FdnReverbProcessor>(sr); }
);
} // namespace

} // namespace audio
