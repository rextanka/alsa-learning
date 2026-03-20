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

namespace audio {

class DistortionProcessor : public Processor {
public:
    static constexpr int   kOversample  = 4;
    static constexpr float kRampSeconds = 0.010f;

    explicit DistortionProcessor(int sample_rate);

    void reset() override {
        aa_[0]  = 0.0f;
        aa_[1]  = 0.0f;
        x_prev_ = 0.0f;
    }

    bool apply_parameter(const std::string& name, float value) override;

protected:
    void do_pull(std::span<float> output, const VoiceContext* ctx = nullptr) override;

private:
    // character=0: symmetric tanh — soft, warm, odd harmonics
    // character=1: asymmetric — positive peaks at +0.5 (tanh×4×0.5),
    //              negative peaks at -1.0 (normal tanh). ~6 dB level
    //              asymmetry generates even harmonics, audible at any drive.
    static float waveshape(float x, float character) noexcept;

    int   ramp_samples_;
    float g_aa_ = 0.0f;

    SmoothedParam drive_{8.0f};
    SmoothedParam character_{0.3f};

    float aa_[2]  = {};
    float x_prev_ = 0.0f;
};

} // namespace audio

#endif // DISTORTION_PROCESSOR_HPP
