/**
 * @file CemFilterProcessor.hpp
 * @brief SH-style CEM/IR3109 4-pole low-pass filter, 24 dB/oct.
 *
 * RT-SAFE chain node: PORT_AUDIO in → PORT_AUDIO out.
 * CV routing: apply_parameter("cutoff_cv", delta) applies 1V/oct exponential
 * modulation each block — base_cutoff_ is preserved; g_ is updated in-place.
 *
 * Type name: SH_FILTER
 *
 * Character: "resonant and liquid-like" — this SH-style filter uses the
 * IR3109 (CEM3340-era) chip. Unlike the Moog-style heavy tanh saturation in
 * the feedback path, the CEM chip operates with a softer algebraic clip in
 * feedback and fully linear stage updates. The result is a cleaner, more
 * stable self-oscillation and a characteristically open high end at moderate
 * resonance — the solid punchy quality preferred for lead lines and bass patches.
 */

#ifndef CEM_FILTER_PROCESSOR_HPP
#define CEM_FILTER_PROCESSOR_HPP

#include "../Processor.hpp"
#include <cmath>
#include <algorithm>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace audio {

/**
 * @brief SH-style / CEM / IR3109 4-pole ladder low-pass (24 dB/oct).
 *
 * Topology: 4 linear 1-pole stages; feedback uses a soft algebraic clip
 * `x / (1 + |x|)` — this saturates more gently than tanh, preserving more
 * high-frequency content in the feedback and giving the cleaner CEM character.
 */
class CemFilterProcessor : public Processor {
public:
    explicit CemFilterProcessor(int sample_rate)
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

        declare_parameter({"cutoff",    "Cutoff Frequency", 20.0f, 20000.0f, 20000.0f, true});
        declare_parameter({"resonance", "Resonance",         0.0f,     1.0f,     0.0f});
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
        // Algebraic soft-clip feedback: x/(1+|x|) — gentler than tanh, cleaner CEM character
        float feedback = stage_[3] * res_ * 4.0f;
        float soft_fb  = feedback / (1.0f + std::abs(feedback));
        float input    = sample - soft_fb;

        // Linear stage updates (no per-stage saturation — CEM/IR3109 characteristic)
        stage_[0] += g_ * (input     - stage_[0]);
        stage_[1] += g_ * (stage_[0] - stage_[1]);
        stage_[2] += g_ * (stage_[1] - stage_[2]);
        stage_[3] += g_ * (stage_[2] - stage_[3]);

        sample = stage_[3];
    }

    void update_g(float cutoff) {
        g_ = std::tan(static_cast<float>(M_PI) * cutoff / static_cast<float>(sample_rate_));
        g_ = std::clamp(g_, 0.0001f, 0.9999f);
    }

    int   sample_rate_;
    float base_cutoff_;
    float base_res_;
    float res_ = 0.0f;
    float g_   = 0.0f;
    float stage_[4];
};

} // namespace audio

#endif // CEM_FILTER_PROCESSOR_HPP
