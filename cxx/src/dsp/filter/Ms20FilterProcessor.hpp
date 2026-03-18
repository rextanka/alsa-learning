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
    explicit Ms20FilterProcessor(int sample_rate)
        : VcfBase(sample_rate)
    {
        hp_lp_ = hp_bp_ = lp_lp_ = lp_bp_ = 0.0f;

        // HP section cutoff is MS-20-specific — not part of the common base
        declare_parameter({"cutoff_hp", "HP Cutoff (Hz)", 20.0f, 2000.0f, 80.0f, true});

        // Initialise coefficients from defaults
        update_cutoff_coefficient(base_cutoff_.get());  // LP section
        update_hp(cutoff_hp_.get());
        update_q();
    }

    bool apply_parameter(const std::string& name, float value) override {
        if (name == "cutoff_hp") {
            cutoff_hp_.set_target(std::clamp(value, 20.0f, static_cast<float>(sample_rate_) * 0.45f), ramp_samples_);
            update_hp(cutoff_hp_.get());
            return true;
        }
        // All other params (cutoff, resonance/res, cutoff_cv, kybd_cv, res_cv)
        // are handled by VcfBase.
        return VcfBase::apply_parameter(name, value);
    }

protected:
    // VcfBase hook: recompute SVF damping on resonance change
    void on_resonance_changed() override { update_q(); }

    // VcfBase pure virtual: update LP section coefficient
    void update_cutoff_coefficient(float cutoff) override {
        f_lp_ = svf_f(cutoff);
    }

    void reset_state() override {
        hp_lp_ = hp_bp_ = lp_lp_ = lp_bp_ = 0.0f;
    }

    void process_sample(float& sample) override {
        // Stage 1: HP section (removes mud below cutoff_hp)
        float hp_out    = sample - q_ * hp_bp_ - hp_lp_;
        float hp_bp_new = f_hp_ * hp_out + hp_bp_;
        float hp_lp_new = f_hp_ * hp_bp_new + hp_lp_;
        hp_bp_ = hp_bp_new;
        hp_lp_ = hp_lp_new;

        // Stage 2: LP section (HP output feeds LP input)
        float lp_hp     = hp_out - q_ * lp_bp_ - lp_lp_;
        float lp_bp_new = f_lp_ * lp_hp + lp_bp_;
        float lp_lp_new = f_lp_ * lp_bp_new + lp_lp_;
        lp_bp_ = lp_bp_new;
        lp_lp_ = lp_lp_new;

        sample = lp_lp_;
    }

private:
    // Chamberlin SVF coefficient: f = 2*sin(π·fc/fs), capped for stability
    float svf_f(float cutoff) const {
        float f = 2.0f * std::sin(static_cast<float>(M_PI) * cutoff
                                  / static_cast<float>(sample_rate_));
        return std::clamp(f, 0.0001f, 1.99f);
    }

    void update_hp(float cutoff) { f_hp_ = svf_f(cutoff); }

    void update_q() {
        // q_ = 2*(1 - res*0.99); at res=1, q_→0.02 → maximum resonance / self-oscillation.
        q_ = 2.0f * (1.0f - res_ * 0.99f);
    }

    SmoothedParam cutoff_hp_{80.0f}; ///< HP section cutoff (Hz), smoothed
    float f_lp_ = 0.0f; ///< LP section SVF coefficient
    float f_hp_ = 0.0f; ///< HP section SVF coefficient
    float q_    = 2.0f; ///< shared damping coefficient

    // SVF state
    float hp_lp_ = 0.0f, hp_bp_ = 0.0f;
    float lp_lp_ = 0.0f, lp_bp_ = 0.0f;
};

} // namespace audio

#endif // MS20_FILTER_PROCESSOR_HPP
