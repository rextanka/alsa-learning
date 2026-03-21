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
#include "../SmoothedParam.hpp"
#include "../TempoSync.hpp"
#include <algorithm>
#include <span>

namespace audio {

class PhaserProcessor : public Processor {
public:
    static constexpr int kMaxStages = 8;
    static constexpr float kRampSeconds = 0.010f; // 10 ms

    explicit PhaserProcessor(int sample_rate);

    void reset() override {
        for (int s = 0; s < kMaxStages; ++s) {
            xl_prev_[s] = yl_prev_[s] = 0.0f;
            xr_prev_[s] = yr_prev_[s] = 0.0f;
        }
        phase_l_ = 0.0;
        phase_r_ = 0.25;
        fb_l_ = fb_r_ = 0.0f;
    }

    bool apply_parameter(const std::string& name, float value) override;

    PortType output_port_type() const override { return PORT_AUDIO; }

protected:
    void do_pull(std::span<float>, const VoiceContext* = nullptr) override {}
    void do_pull(AudioBuffer& buf, const VoiceContext* ctx = nullptr) override;

private:
    float allpass_coeff(float fc) const;
    float process_cascade(float in, float a1,
                          float x_prev[kMaxStages],
                          float y_prev[kMaxStages]);

    int   sample_rate_;
    int   ramp_samples_;

    SmoothedParam rate_{0.5f};
    SmoothedParam depth_{1.5f};
    SmoothedParam feedback_{0.7f};
    SmoothedParam base_freq_{400.0f};
    int   stages_    = 4;
    SmoothedParam wet_{0.5f};

    double phase_l_ = 0.0;
    double phase_r_ = 0.25;

    float xl_prev_[kMaxStages]{};
    float yl_prev_[kMaxStages]{};
    float xr_prev_[kMaxStages]{};
    float yr_prev_[kMaxStages]{};
    float fb_l_ = 0.0f;
    float fb_r_ = 0.0f;

    // Tempo-sync (Phase 27D) — off by default, fully backward-compatible.
    bool sync_     = false;
    int  division_ = 1; ///< Index into kDivisionMultipliers; 1 = "half" (spec default)
};

} // namespace audio

#endif // PHASER_PROCESSOR_HPP
