/**
 * @file PhaserProcessor.hpp
 * @brief 4-stage (or 8-stage) all-pass phaser with stereo quadrature LFO.
 *
 * Each stage is a 1st-order all-pass filter:
 *   y[n] = a1 * (x[n] - y[n-1]) + x[n-1],  a1 = (tan(π·fc/sr) - 1) / (tan(π·fc/sr) + 1)
 *
 * An LFO sweeps the all-pass pole frequency `fc` around `base_freq`:
 *   fc = base_freq * 2^(depth * lfo)
 * where lfo ∈ [-1, 1] (sine).  The LFO range is ±`depth` octaves around base_freq.
 *
 * Stereo: left channel uses LFO at phase 0°, right channel at 90° (quadrature),
 * giving a slow rotating / orbiting image.
 *
 * Feedback: the cascaded all-pass output is fed back to the input, creating the
 * characteristic notch comb.  Higher feedback = deeper, narrower notches.
 *
 * Parameters:
 *   rate       0.01–20.0 Hz  LFO rate
 *   depth      0.0–4.0 oct   LFO sweep depth in octaves around base_freq
 *   feedback   0.0–0.95      feedback fraction (negative = invert phase)
 *   base_freq  20–4000 Hz    center frequency for LFO sweep
 *   stages     4 or 8        all-pass stage count
 *   wet        0.0–1.0       wet/dry mix (0.5 = classic phaser blend)
 */

#ifndef PHASER_PROCESSOR_HPP
#define PHASER_PROCESSOR_HPP

#include "../Processor.hpp"
#include <cmath>
#include <algorithm>
#include <span>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace audio {

class PhaserProcessor : public Processor {
public:
    static constexpr int kMaxStages = 8;

    explicit PhaserProcessor(int sample_rate)
        : sample_rate_(sample_rate)
    {
        declare_port({"audio_in",  PORT_AUDIO, PortDirection::IN});
        declare_port({"audio_out", PORT_AUDIO, PortDirection::OUT});
        declare_parameter({"rate",      "LFO Rate (Hz)",     0.01f, 20.0f,  0.5f, true});
        declare_parameter({"depth",     "Depth (oct)",        0.0f,  4.0f,  1.5f});
        declare_parameter({"feedback",  "Feedback",          -0.95f, 0.95f, 0.7f});
        declare_parameter({"base_freq", "Base Freq (Hz)",    20.0f,4000.0f,400.0f});
        declare_parameter({"stages",    "Stages (4 or 8)",    4.0f,  8.0f,  4.0f});
        declare_parameter({"wet",       "Wet/Dry",            0.0f,  1.0f,  0.5f});
    }

    void reset() override {
        for (int s = 0; s < kMaxStages; ++s) {
            xl_prev_[s] = yl_prev_[s] = 0.0f;
            xr_prev_[s] = yr_prev_[s] = 0.0f;
        }
        phase_l_ = 0.0;
        phase_r_ = 0.25;  // 90° quadrature offset
        fb_l_ = fb_r_ = 0.0f;
    }

    bool apply_parameter(const std::string& name, float value) override {
        if (name == "rate")      { rate_      = std::max(0.01f, value);                   return true; }
        if (name == "depth")     { depth_     = std::clamp(value, 0.0f, 4.0f);            return true; }
        if (name == "feedback")  { feedback_  = std::clamp(value, -0.95f, 0.95f);         return true; }
        if (name == "base_freq") { base_freq_ = std::clamp(value, 20.0f, 4000.0f);        return true; }
        if (name == "stages")    { stages_    = (value >= 6.5f) ? 8 : 4;                  return true; }
        if (name == "wet")       { wet_       = std::clamp(value, 0.0f, 1.0f);            return true; }
        return false;
    }

    PortType output_port_type() const override { return PORT_AUDIO; }

protected:
    void do_pull(std::span<float>, const VoiceContext* = nullptr) override {}

    void do_pull(AudioBuffer& buf, const VoiceContext* = nullptr) override {
        const float dry        = 1.0f - wet_;
        const float phase_inc  = rate_ / static_cast<float>(sample_rate_);
        const size_t frames    = buf.frames();

        for (size_t f = 0; f < frames; ++f) {
            // Advance LFO phases
            phase_l_ += phase_inc;
            if (phase_l_ >= 1.0) phase_l_ -= 1.0;
            phase_r_ += phase_inc;
            if (phase_r_ >= 1.0) phase_r_ -= 1.0;

            // Modulated pole frequency
            const float lfo_l = static_cast<float>(std::sin(2.0 * M_PI * phase_l_));
            const float lfo_r = static_cast<float>(std::sin(2.0 * M_PI * phase_r_));
            const float fc_l  = base_freq_ * std::pow(2.0f, depth_ * lfo_l);
            const float fc_r  = base_freq_ * std::pow(2.0f, depth_ * lfo_r);

            // All-pass coefficient
            const float a1_l = allpass_coeff(std::clamp(fc_l, 1.0f, static_cast<float>(sample_rate_) * 0.49f));
            const float a1_r = allpass_coeff(std::clamp(fc_r, 1.0f, static_cast<float>(sample_rate_) * 0.49f));

            // Process L: feedback → all-pass cascade
            float inL = buf.left[f] + fb_l_ * feedback_;
            float outL = process_cascade(inL, a1_l, xl_prev_, yl_prev_);
            fb_l_ = outL;

            // Process R: feedback → all-pass cascade
            float inR = buf.right[f] + fb_r_ * feedback_;
            float outR = process_cascade(inR, a1_r, xr_prev_, yr_prev_);
            fb_r_ = outR;

            buf.left[f]  = outL * wet_ + buf.left[f]  * dry;
            buf.right[f] = outR * wet_ + buf.right[f] * dry;
        }
    }

private:
    // First-order all-pass coefficient from cutoff frequency
    float allpass_coeff(float fc) const {
        const float t = std::tan(static_cast<float>(M_PI) * fc / static_cast<float>(sample_rate_));
        return (t - 1.0f) / (t + 1.0f);
    }

    // Process one sample through the all-pass cascade (stages_ stages)
    float process_cascade(float in, float a1,
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

    int   sample_rate_;
    float rate_      = 0.5f;
    float depth_     = 1.5f;
    float feedback_  = 0.7f;
    float base_freq_ = 400.0f;
    int   stages_    = 4;
    float wet_       = 0.5f;

    double phase_l_ = 0.0;
    double phase_r_ = 0.25;  // 90° quadrature for stereo width

    float xl_prev_[kMaxStages]{};
    float yl_prev_[kMaxStages]{};
    float xr_prev_[kMaxStages]{};
    float yr_prev_[kMaxStages]{};
    float fb_l_ = 0.0f;
    float fb_r_ = 0.0f;
};

} // namespace audio

#endif // PHASER_PROCESSOR_HPP
