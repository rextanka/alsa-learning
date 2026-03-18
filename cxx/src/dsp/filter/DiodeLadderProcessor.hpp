/**
 * @file DiodeLadderProcessor.hpp
 * @brief TB-style diode ladder filter — "rubbery acid" character.
 *
 * RT-SAFE chain node: PORT_AUDIO in → PORT_AUDIO out.
 * CV routing: apply_parameter("cutoff_cv", delta) applies 1V/oct exponential
 * modulation each block — base_cutoff_ is preserved; g_ is updated in-place.
 *
 * TB-style character note: The classic TB-style circuit is technically a
 * 4-pole (24 dB/oct) diode ladder, but component tolerances and the resonance
 * feedback path cause it to behave more like an 18 dB/oct (3-pole) filter at
 * moderate resonance. This is emulated here by blending the 3-pole output
 * (stage_[2]) with the 4-pole output (stage_[3]): the blend shifts from ~50/50
 * at zero resonance to 100% 3-pole at maximum resonance, giving the
 * characteristic rubbery squelch.
 */

#ifndef AUDIO_DIODE_LADDER_PROCESSOR_HPP
#define AUDIO_DIODE_LADDER_PROCESSOR_HPP

#include "../Processor.hpp"
#include <cmath>
#include <algorithm>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace audio {

/**
 * @brief Diode-ladder low-pass filter with TB-style 3/4-pole blend (18–24 dB/oct).
 *
 * Topology: 4 tanh-saturated stages (non-linear each stage), resonance feedback
 * from a blend of stage[2] (3-pole) and stage[3] (4-pole). At full resonance
 * the 3-pole output dominates, mimicking the 303's measured 18 dB/oct rolloff
 * and characteristic "missing top-end" squelch.
 */
class DiodeLadderProcessor : public Processor {
public:
    explicit DiodeLadderProcessor(int sample_rate)
        : sample_rate_(sample_rate)
        , base_cutoff_(20000.0f)
        , base_res_(0.0f)
    {
        for (int i = 0; i < 4; ++i) stage_[i] = 0.0f;
        update_g(base_cutoff_);

        declare_port({"audio_in",  PORT_AUDIO,   PortDirection::IN});
        declare_port({"audio_out", PORT_AUDIO,   PortDirection::OUT});
        declare_port({"cutoff_cv", PORT_CONTROL, PortDirection::IN, false}); // bipolar, 1V/oct
        declare_port({"res_cv",    PORT_CONTROL, PortDirection::IN, true});  // unipolar additive
        declare_port({"kybd_cv",   PORT_CONTROL, PortDirection::IN, false}); // bipolar, 1V/oct
        declare_port({"fm_in",     PORT_AUDIO,   PortDirection::IN});        // audio-rate FM

        declare_parameter({"cutoff",     "Cutoff Frequency", 20.0f, 20000.0f, 20000.0f, true});
        declare_parameter({"resonance",  "Resonance",         0.0f,     1.0f,     0.0f});
        declare_parameter({"hpf_cutoff", "HPF Stage (0-3)",   0.0f,     3.0f,     0.0f});
    }

    bool apply_parameter(const std::string& name, float value) override {
        if (name == "cutoff") {
            base_cutoff_ = std::clamp(value, 20.0f, sample_rate_ * 0.45f);
            update_g(base_cutoff_);
            return true;
        }
        if (name == "resonance" || name == "res") {
            base_res_ = std::clamp(value, 0.0f, 1.0f);
            res_       = base_res_;
            return true;
        }
        if (name == "cutoff_cv") {
            float eff = std::max(20.0f, base_cutoff_ * std::pow(2.0f, value));
            update_g(eff);
            return true;
        }
        if (name == "res_cv") {
            res_ = std::clamp(base_res_ + value, 0.0f, 1.0f);
            return true;
        }
        if (name == "hpf_cutoff") { hpf_cutoff_ = value; return true; }
        return false;
    }

    void reset() override {
        for (int i = 0; i < 4; ++i) stage_[i] = 0.0f;
    }

protected:
    void do_pull(std::span<float> output,
                 const VoiceContext* /* ctx */ = nullptr) override {
        for (auto& sample : output) process_sample(sample);
    }

    void do_pull(AudioBuffer& output,
                 const VoiceContext* /* ctx */ = nullptr) override {
        for (size_t i = 0; i < output.frames(); ++i) {
            float s = (output.left[i] + output.right[i]) * 0.5f;
            process_sample(s);
            output.left[i] = output.right[i] = s;
        }
    }

private:
    inline void process_sample(float& sample) {
        // TB-style pole blend: at full resonance, 3-pole (stage_[2]) dominates
        // over 4-pole (stage_[3]), giving the ~18 dB/oct rubbery character.
        float pole3 = stage_[2];
        float pole4 = stage_[3];
        float blend  = 1.0f - res_;                  // 0 at full res, 1 at zero res
        float tap    = pole3 + blend * (pole4 - pole3); // lerp toward stage_[3] at low res

        float feedback = std::tanh(tap * res_ * 3.5f);
        float input    = sample - feedback;

        float s0 = stage_[0], s1 = stage_[1], s2 = stage_[2], s3 = stage_[3];
        stage_[0] += g_ * (std::tanh(input)     - std::tanh(s0));
        stage_[1] += g_ * (std::tanh(stage_[0]) - std::tanh(s1));
        stage_[2] += g_ * (std::tanh(stage_[1]) - std::tanh(s2));
        stage_[3] += g_ * (std::tanh(stage_[2]) - std::tanh(s3));

        // Output: same blend logic on the updated stages — 3-pole at high res
        float out3 = stage_[2];
        float out4 = stage_[3];
        sample = out3 + blend * (out4 - out3);

        if (std::isnan(sample) || std::isinf(sample)) { reset(); sample = 0.0f; }
    }

    void update_g(float cutoff) {
        float f = cutoff / static_cast<float>(sample_rate_);
        g_ = std::tan(static_cast<float>(M_PI) * f);
        g_ = std::clamp(g_, 0.0001f, 0.9999f);
    }

    int   sample_rate_;
    float base_cutoff_;
    float base_res_;
    float res_ = 0.0f;
    float g_   = 0.0f;
    float stage_[4];
    float hpf_cutoff_ = 0.0f;
};

} // namespace audio

#endif // AUDIO_DIODE_LADDER_PROCESSOR_HPP
