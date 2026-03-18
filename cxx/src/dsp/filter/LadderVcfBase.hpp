/**
 * @file LadderVcfBase.hpp
 * @brief Intermediate base for 4-pole transistor/diode ladder filters.
 *
 * Extends VcfBase with the shared topology of MOOG_FILTER, DIODE_FILTER,
 * and SH_FILTER:
 *   - 4 cascaded 1-pole stages (stage_[4])
 *   - Bilinear-prewarped frequency coefficient g_ = tan(π·fc/fs)
 *   - fm_in PORT_AUDIO input port (audio-rate cutoff FM, declared for all three)
 *
 * Implements:
 *   update_cutoff_coefficient(float)  — updates g_ via tan()
 *   reset_state()                     — zeros stage_[4]
 *
 * Subclasses (Moog, Diode, CEM) implement only process_sample(float&).
 * Moog and Diode additionally declare hpf_cutoff parameter and override
 * apply_parameter to handle it before delegating to this base.
 */

#ifndef LADDER_VCF_BASE_HPP
#define LADDER_VCF_BASE_HPP

#include "VcfBase.hpp"

namespace audio {

class LadderVcfBase : public VcfBase {
public:
    explicit LadderVcfBase(int sample_rate)
        : VcfBase(sample_rate)
    {
        for (int i = 0; i < 4; ++i) stage_[i] = 0.0f;

        declare_port({"fm_in", PORT_AUDIO, PortDirection::IN}); // audio-rate cutoff FM
        declare_parameter({"fm_depth", "FM Depth", 0.0f, 1.0f, 0.0f});

        // Initialise g_ from the default base_cutoff_
        update_cutoff_coefficient(base_cutoff_);
    }

    /**
     * @brief Inject the fm_in audio buffer for audio-rate cutoff modulation.
     *
     * When connected, the signed audio signal is scaled by fm_depth_ and
     * added to the effective cutoff (in octaves) per sample in do_pull.
     */
    void inject_audio(std::string_view port_name,
                      std::span<const float> audio) override {
        if (port_name == "fm_in") fm_in_ = audio;
    }

    bool apply_parameter(const std::string& name, float value) override {
        if (name == "fm_depth") { fm_depth_ = std::clamp(value, 0.0f, 1.0f); return true; }
        return VcfBase::apply_parameter(name, value);
    }

protected:
    void update_cutoff_coefficient(float cutoff) override {
        // Bilinear pre-warp: g = tan(π·fc/fs), clamped for numerical stability
        g_ = std::tan(static_cast<float>(M_PI) * cutoff / static_cast<float>(sample_rate_));
        g_ = std::clamp(g_, 0.0001f, 0.9999f);
    }

    void reset_state() override {
        for (int i = 0; i < 4; ++i) stage_[i] = 0.0f;
    }

    // Override do_pull to handle per-sample audio-rate FM when fm_in_ is present.
    void do_pull(std::span<float> output,
                 const VoiceContext* ctx = nullptr) override {
        if (fm_in_.empty() || fm_depth_ == 0.0f) {
            VcfBase::do_pull(output, ctx);
        } else {
            for (size_t i = 0; i < output.size(); ++i) {
                const float eff_cv = cutoff_cv_ + kybd_cv_ + fm_depth_ * fm_in_[i];
                const float fc = std::max(20.0f, base_cutoff_ * std::pow(2.0f, eff_cv));
                update_cutoff_coefficient(fc);
                process_sample(output[i]);
            }
            // Restore coefficient to block-rate value after FM loop.
            update_effective_cutoff();
            fm_in_ = {};
        }
    }

    float stage_[4];
    float g_ = 0.0f; ///< frequency coefficient (pre-warped via tan)

private:
    std::span<const float> fm_in_;   ///< injected audio-rate FM signal (per-block)
    float fm_depth_ = 0.0f;          ///< 0–1 scale factor on fm_in
};

} // namespace audio

#endif // LADDER_VCF_BASE_HPP
