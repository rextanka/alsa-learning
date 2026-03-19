/**
 * @file DistortionProcessor.hpp
 * @brief Guitar/pedal-style distortion with 4× oversampling.
 *
 * Type name: DISTORTION
 *
 * Signal path:
 *   drive → 4× oversample (linear interp) → waveshape → 2-pole IIR AA → decimate
 *
 * Parameters:
 *   drive      1–40   (default 8)    input gain into the clipping stage
 *   character  0–1    (default 0.3)  waveshaper blend:
 *                                      0 = symmetric tanh (soft/tube, odd harmonics)
 *                                      1 = asymmetric clip: positive peaks at +0.5,
 *                                          negative at -1.0 (even + odd harmonics)
 *                                    The 6 dB asymmetry is clearly audible at any drive.
 *
 * Note on pre-emphasis: not implemented here — if you want bass-cut before
 * the distortion stage, place a HIGH_PASS_FILTER module before this in the patch.
 */

#ifndef DISTORTION_PROCESSOR_HPP
#define DISTORTION_PROCESSOR_HPP

#include "../Processor.hpp"
#include "../SmoothedParam.hpp"
#include <algorithm>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace audio {

class DistortionProcessor : public Processor {
public:
    static constexpr int   kOversample  = 4;
    static constexpr float kRampSeconds = 0.010f;

    explicit DistortionProcessor(int sample_rate)
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

    void reset() override {
        aa_[0]  = 0.0f;
        aa_[1]  = 0.0f;
        x_prev_ = 0.0f;
    }

    bool apply_parameter(const std::string& name, float value) override {
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

protected:
    void do_pull(std::span<float> output,
                 const VoiceContext* /*ctx*/ = nullptr) override {
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

private:
    // character=0: symmetric tanh — soft, warm, odd harmonics
    // character=1: asymmetric — positive peaks at +0.5 (tanh×4×0.5),
    //              negative peaks at -1.0 (normal tanh). ~6 dB level
    //              asymmetry generates even harmonics, audible at any drive.
    static float waveshape(float x, float character) noexcept {
        const float soft = std::tanh(x);
        const float hard = (x >= 0.0f)
            ? std::tanh(x * 4.0f) * 0.5f
            : std::tanh(x);
        return soft + character * (hard - soft);
    }

    int   ramp_samples_;
    float g_aa_ = 0.0f;

    SmoothedParam drive_{8.0f};
    SmoothedParam character_{0.3f};

    float aa_[2]  = {};
    float x_prev_ = 0.0f;
};

} // namespace audio

#endif // DISTORTION_PROCESSOR_HPP
