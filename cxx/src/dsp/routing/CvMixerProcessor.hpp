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
 * Inputs are populated via inject_cv() before do_pull(). Any un-injected slot
 * contributes zero. Source mod_sources must precede this node in mod_sources_.
 *
 * Usage — delayed vibrato:
 *   LFO:control_out  → CV_MIXER:cv_in_1  (gain_1 = LFO depth)
 *   ENV2:envelope_out → CV_MIXER:cv_in_2  (gain_2 = overall depth, slow attack = onset delay)
 *   CV_MIXER:cv_out  → VCO:pitch_cv
 */

#ifndef CV_MIXER_PROCESSOR_HPP
#define CV_MIXER_PROCESSOR_HPP

#include "../Processor.hpp"
#include <algorithm>
#include <cstring>

namespace audio {

class CvMixerProcessor : public Processor {
public:
    CvMixerProcessor() {
        declare_port({"cv_in_1", PORT_CONTROL, PortDirection::IN,  false});
        declare_port({"cv_in_2", PORT_CONTROL, PortDirection::IN,  false});
        declare_port({"cv_in_3", PORT_CONTROL, PortDirection::IN,  false});
        declare_port({"cv_in_4", PORT_CONTROL, PortDirection::IN,  false});
        declare_port({"cv_out",  PORT_CONTROL, PortDirection::OUT, false});

        declare_parameter({"gain_1", "Gain 1", -1.0f, 1.0f, 1.0f});
        declare_parameter({"gain_2", "Gain 2", -1.0f, 1.0f, 1.0f});
        declare_parameter({"gain_3", "Gain 3", -1.0f, 1.0f, 1.0f});
        declare_parameter({"gain_4", "Gain 4", -1.0f, 1.0f, 1.0f});
        declare_parameter({"offset", "DC Offset", -1.0f, 1.0f, 0.0f});
    }

    PortType output_port_type() const override { return PortType::PORT_CONTROL; }

    void reset() override {
        for (auto& s : slots_) s = {};
    }

    bool apply_parameter(const std::string& name, float value) override {
        if (name == "gain_1") { gain_[0] = value; return true; }
        if (name == "gain_2") { gain_[1] = value; return true; }
        if (name == "gain_3") { gain_[2] = value; return true; }
        if (name == "gain_4") { gain_[3] = value; return true; }
        if (name == "offset") { offset_ = value;  return true; }
        return false;
    }

    void inject_cv(std::string_view port_name, std::span<const float> cv) override {
        if      (port_name == "cv_in_1") slots_[0] = cv;
        else if (port_name == "cv_in_2") slots_[1] = cv;
        else if (port_name == "cv_in_3") slots_[2] = cv;
        else if (port_name == "cv_in_4") slots_[3] = cv;
    }

protected:
    void do_pull(std::span<float> output,
                 const VoiceContext* /*ctx*/ = nullptr) override {
        for (size_t i = 0; i < output.size(); ++i) {
            float v = offset_;
            for (int s = 0; s < 4; ++s) {
                if (i < slots_[s].size())
                    v += gain_[s] * slots_[s][i];
            }
            output[i] = std::clamp(v, -1.0f, 1.0f);
        }
        for (auto& s : slots_) s = {}; // clear after use
    }

private:
    float gain_[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    float offset_  = 0.0f;
    std::span<const float> slots_[4];
};

} // namespace audio

#endif // CV_MIXER_PROCESSOR_HPP
