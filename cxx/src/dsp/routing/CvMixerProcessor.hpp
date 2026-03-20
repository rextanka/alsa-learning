/**
 * @file CvMixerProcessor.hpp
 * @brief CV mixer / attenuverter — sums up to 4 control signals with independent gains.
 *
 * Type name: CV_MIXER
 *
 * Ports (PORT_CONTROL):
 *   cv_in_1 … cv_in_4  IN  bipolar [-1,1]
 *   cv_out              OUT bipolar [-1,1]
 *
 * Parameters:
 *   gain_1 … gain_4  (-1.0 – 1.0, default 1.0) — per-input scale; negative = inversion
 *   offset           (-1.0 – 1.0, default 0.0) — DC bias added to the sum
 *
 * Output = clamp(gain_1*in_1 + gain_2*in_2 + gain_3*in_3 + gain_4*in_4 + offset, -1, 1)
 *
 * Usage — delayed vibrato:
 *   LFO:control_out  → CV_MIXER:cv_in_1  (gain_1 = LFO depth)
 *   ENV2:envelope_out → CV_MIXER:cv_in_2  (gain_2 = overall depth, slow attack = onset delay)
 *   CV_MIXER:cv_out  → VCO:pitch_cv
 */

#ifndef CV_MIXER_PROCESSOR_HPP
#define CV_MIXER_PROCESSOR_HPP

#include "../Processor.hpp"
#include "../SmoothedParam.hpp"
#include <algorithm>

namespace audio {

class CvMixerProcessor : public Processor {
public:
    static constexpr float kRampSeconds = 0.010f;

    explicit CvMixerProcessor(int sample_rate = 48000);

    PortType output_port_type() const override { return PortType::PORT_CONTROL; }

    void reset() override { for (auto& s : slots_) s = {}; }

    bool apply_parameter(const std::string& name, float value) override {
        // CV routing gains snap immediately — patch-configuration values set once
        // before audio. CV inputs are already smoothed by their source.
        if (name == "gain_1") { gain_[0].set_target(value, 0); return true; }
        if (name == "gain_2") { gain_[1].set_target(value, 0); return true; }
        if (name == "gain_3") { gain_[2].set_target(value, 0); return true; }
        if (name == "gain_4") { gain_[3].set_target(value, 0); return true; }
        if (name == "offset") { offset_.set_target(value, 0);  return true; }
        return false;
    }

    void inject_cv(std::string_view port_name, std::span<const float> cv) override {
        if      (port_name == "cv_in_1") slots_[0] = cv;
        else if (port_name == "cv_in_2") slots_[1] = cv;
        else if (port_name == "cv_in_3") slots_[2] = cv;
        else if (port_name == "cv_in_4") slots_[3] = cv;
    }

protected:
    void do_pull(std::span<float> output, const VoiceContext* ctx = nullptr) override;

private:
    int ramp_samples_ = 480;
    SmoothedParam gain_[4] = {SmoothedParam{1.0f}, SmoothedParam{1.0f}, SmoothedParam{1.0f}, SmoothedParam{1.0f}};
    SmoothedParam offset_{0.0f};
    std::span<const float> slots_[4];
};

} // namespace audio

#endif // CV_MIXER_PROCESSOR_HPP
