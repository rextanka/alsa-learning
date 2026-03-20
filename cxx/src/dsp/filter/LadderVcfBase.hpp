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
        update_cutoff_coefficient(base_cutoff_.get());
    }

    /**
     * @brief Inject the fm_in audio buffer for audio-rate cutoff modulation.
     */
    void inject_audio(std::string_view port_name,
                      std::span<const float> audio) override {
        if (port_name == "fm_in") fm_in_ = audio;
    }

    bool apply_parameter(const std::string& name, float value) override {
        if (name == "fm_depth") { fm_depth_.set_target(std::clamp(value, 0.0f, 1.0f), ramp_samples_); return true; }
        return VcfBase::apply_parameter(name, value);
    }

protected:
    void update_cutoff_coefficient(float cutoff) override {
        g_ = std::tan(static_cast<float>(M_PI) * cutoff / static_cast<float>(sample_rate_));
        g_ = std::clamp(g_, 0.0001f, 0.9999f);
    }

    void reset_state() override {
        for (int i = 0; i < 4; ++i) stage_[i] = 0.0f;
    }

    // FM-aware do_pull override — implemented in LadderVcfBase.cpp
    void do_pull(std::span<float> output, const VoiceContext* ctx = nullptr) override;

    float stage_[4];
    float g_ = 0.0f; ///< frequency coefficient (pre-warped via tan)

private:
    std::span<const float> fm_in_;        ///< injected audio-rate FM signal (per-block)
    SmoothedParam fm_depth_{0.0f};        ///< 0–1 scale factor on fm_in (smoothed)
};

} // namespace audio

#endif // LADDER_VCF_BASE_HPP
