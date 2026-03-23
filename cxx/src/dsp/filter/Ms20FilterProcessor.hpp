/**
 * @file Ms20FilterProcessor.hpp
 * @brief MS-style dual 2-pole (12 dB/oct) HP + LP filter in series.
 *
 * RT-SAFE chain node: PORT_AUDIO in → PORT_AUDIO out.
 * Type name: MS20_FILTER
 *
 * Character: "aggressive, screaming, gritty" — dual Chamberlin SVF topology:
 * a high-pass section (cutoff_hp, default 80 Hz) followed by a low-pass section
 * (cutoff). Each section is 12 dB/oct (2-pole); the shallow slope means more
 * harmonics bleed through at moderate resonance, contributing to perceived
 * dirtiness. LP self-oscillates at resonance ≈ 0.9.
 *
 * Topology: input → HP (2-pole, cutoff_hp) → LP (2-pole, cutoff) → output
 *
 * Parameters beyond the VcfBase common set:
 *   cutoff_hp (20–2000 Hz, log — HP section, default 80 Hz)
 */

#ifndef MS20_FILTER_PROCESSOR_HPP
#define MS20_FILTER_PROCESSOR_HPP

#include "VcfBase.hpp"

namespace audio {

class Ms20FilterProcessor : public VcfBase {
public:
    explicit Ms20FilterProcessor(int sample_rate);

    bool apply_parameter(const std::string& name, float value) override;
    void inject_audio(std::string_view port_name, std::span<const float> audio) override;

protected:
    void on_resonance_changed() override { update_q(); }
    void update_cutoff_coefficient(float cutoff) override { f_lp_ = svf_f(cutoff); }
    void reset_state() override { hp_lp_ = hp_bp_ = lp_lp_ = lp_bp_ = 0.0f; }
    void process_sample(float& sample) override;
    void do_pull(std::span<float> output, const VoiceContext* ctx = nullptr) override;

private:
    float svf_f(float cutoff) const;
    void update_hp(float cutoff) { f_hp_ = svf_f(cutoff); }
    void update_q() { q_ = 2.0f * (1.0f - res_ * 0.99f); }

    SmoothedParam cutoff_hp_{80.0f};
    float f_lp_ = 0.0f;
    float f_hp_ = 0.0f;
    float q_    = 2.0f;

    float hp_lp_ = 0.0f, hp_bp_ = 0.0f;
    float lp_lp_ = 0.0f, lp_bp_ = 0.0f;

    std::span<const float> fm_in_;       ///< audio-rate cutoff FM (per-block)
    SmoothedParam          fm_depth_{0.0f}; ///< 0–1 scale factor on fm_in
};

} // namespace audio

#endif // MS20_FILTER_PROCESSOR_HPP
