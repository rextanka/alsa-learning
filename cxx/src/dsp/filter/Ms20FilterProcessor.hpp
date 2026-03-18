/**
 * @file Ms20FilterProcessor.hpp
 * @brief MS-style dual 2-pole (12 dB/oct) HP + LP filter in series.
 *
 * RT-SAFE chain node: PORT_AUDIO in → PORT_AUDIO out.
 * CV routing: apply_parameter("cutoff_cv", delta) applies 1V/oct exponential
 * modulation to the LP section each block; apply_parameter("cutoff_hp", hz)
 * controls the HP section cutoff directly.
 *
 * Type name: MS20_FILTER
 *
 * Character: "aggressive, screaming, gritty" — this MS-style filter uses two
 * independent 2-pole (12 dB/oct) Sallen-Key filter sections: a high-pass
 * followed by a low-pass. The shallow slope means more harmonics bleed
 * through at moderate resonance. Self-oscillation in the LP section produces
 * the characteristic screaming quality at high resonance. The HP section
 * removes mud and low-end rumble, contributing to the perceived "dirtiness".
 *
 * Implementation uses the Chamberlin state variable filter (SVF) for each
 * 2-pole section. LP and HP outputs are extracted simultaneously from the
 * same state; the HP output feeds the LP section.
 *
 * Topology:
 *   input → HPF (2-pole, cutoff_hp) → LPF (2-pole, cutoff_lp) → output
 *
 * Parameter mapping:
 *   "cutoff"    → LP section cutoff (Hz) — primary timbral control
 *   "cutoff_hp" → HP section cutoff (Hz, default 80 Hz)
 *   "resonance" → shared resonance / Q; drives both sections; LP self-oscillates ≈ 0.9
 */

#ifndef MS20_FILTER_PROCESSOR_HPP
#define MS20_FILTER_PROCESSOR_HPP

#include "../Processor.hpp"
#include <cmath>
#include <algorithm>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace audio {

/**
 * @brief MS-style dual 2-pole LP+HP Chamberlin SVF (12 dB/oct each).
 *
 * HP section removes low-end mud; LP section sculpts the top end.
 * Resonance drives both sections; LP self-oscillates at maximum resonance.
 */
class Ms20FilterProcessor : public Processor {
public:
    explicit Ms20FilterProcessor(int sample_rate)
        : sample_rate_(sample_rate)
        , base_cutoff_lp_(20000.0f)
        , base_cutoff_hp_(80.0f)
        , base_res_(0.0f)
    {
        hp_lp_ = hp_bp_ = lp_lp_ = lp_bp_ = 0.0f;
        update_lp(base_cutoff_lp_);
        update_hp(base_cutoff_hp_);

        declare_port({"audio_in",   PORT_AUDIO,   PortDirection::IN});
        declare_port({"audio_out",  PORT_AUDIO,   PortDirection::OUT});
        declare_port({"cutoff_cv",  PORT_CONTROL, PortDirection::IN, false}); // bipolar, 1V/oct → LP
        declare_port({"res_cv",     PORT_CONTROL, PortDirection::IN, true});  // unipolar additive
        declare_port({"kybd_cv",    PORT_CONTROL, PortDirection::IN, false}); // bipolar, 1V/oct → LP

        declare_parameter({"cutoff",    "LP Cutoff (Hz)",    20.0f, 20000.0f, 20000.0f, true});
        declare_parameter({"cutoff_hp", "HP Cutoff (Hz)",    20.0f,  2000.0f,    80.0f, true});
        declare_parameter({"resonance", "Resonance",          0.0f,     1.0f,     0.0f});
    }

    bool apply_parameter(const std::string& name, float value) override {
        if (name == "cutoff") {
            base_cutoff_lp_ = std::clamp(value, 20.0f, sample_rate_ * 0.45f);
            update_lp(base_cutoff_lp_);
            return true;
        }
        if (name == "cutoff_hp") {
            base_cutoff_hp_ = std::clamp(value, 20.0f, sample_rate_ * 0.45f);
            update_hp(base_cutoff_hp_);
            return true;
        }
        if (name == "resonance" || name == "res") {
            base_res_ = std::clamp(value, 0.0f, 1.0f);
            res_       = base_res_;
            update_q();
            return true;
        }
        if (name == "cutoff_cv") {
            float eff = std::max(20.0f, base_cutoff_lp_ * std::pow(2.0f, value));
            update_lp(eff);
            return true;
        }
        if (name == "res_cv") {
            res_ = std::clamp(base_res_ + value, 0.0f, 1.0f);
            update_q();
            return true;
        }
        return false;
    }

    void reset() override {
        hp_lp_ = hp_bp_ = lp_lp_ = lp_bp_ = 0.0f;
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
    // Chamberlin SVF: produces HP, BP, LP outputs.
    // hp = input - q*bp - lp
    // bp = f*hp + bp  (new_bp = f*hp + old_bp)
    // lp = f*bp + lp  (new_lp = f*new_bp + old_lp)
    //
    // f = 2 * sin(pi * fc / fs)  [correct for fc < fs/6; capped for stability]
    // q = 1/resonance_Q where Q ∈ [0.5, ∞); q_=0 → self-oscillation

    inline void process_sample(float& sample) {
        // Stage 1: HP section (removes mud below cutoff_hp)
        float hp_out = sample - q_ * hp_bp_ - hp_lp_;
        float hp_bp_new = f_hp_ * hp_out + hp_bp_;
        float hp_lp_new = f_hp_ * hp_bp_new + hp_lp_;
        hp_bp_ = hp_bp_new;
        hp_lp_ = hp_lp_new;
        float after_hp = hp_out; // HP output feeds LP section

        // Stage 2: LP section (sculpts top end, self-oscillates at high Q)
        float lp_hp = after_hp - q_ * lp_bp_ - lp_lp_;
        float lp_bp_new = f_lp_ * lp_hp + lp_bp_;
        float lp_lp_new = f_lp_ * lp_bp_new + lp_lp_;
        lp_bp_ = lp_bp_new;
        lp_lp_ = lp_lp_new;

        sample = lp_lp_; // final output: LP section low-pass output
    }

    // f = 2 * sin(pi * fc / fs), capped at 1.99 for stability
    static float svf_f(float cutoff, int sr) {
        float f = 2.0f * std::sin(static_cast<float>(M_PI) * cutoff / static_cast<float>(sr));
        return std::clamp(f, 0.0001f, 1.99f);
    }

    void update_lp(float cutoff) { f_lp_ = svf_f(cutoff, sample_rate_); }
    void update_hp(float cutoff) { f_hp_ = svf_f(cutoff, sample_rate_); }

    void update_q() {
        // q_ = 2*(1 - resonance); at res=1, q_→0 → maximum resonance / self-oscillation.
        // MS-20 resonance is notably aggressive: use a tighter curve.
        q_ = 2.0f * (1.0f - res_ * 0.99f); // prevents numerical blowup at res=1
    }

    int   sample_rate_;
    float base_cutoff_lp_;
    float base_cutoff_hp_;
    float base_res_;
    float res_ = 0.0f;
    float f_lp_ = 0.0f; // LP section frequency coefficient
    float f_hp_ = 0.0f; // HP section frequency coefficient
    float q_    = 2.0f; // shared damping coefficient (both sections)

    // SVF state — HP section
    float hp_lp_ = 0.0f;
    float hp_bp_ = 0.0f;
    // SVF state — LP section
    float lp_lp_ = 0.0f;
    float lp_bp_ = 0.0f;
};

} // namespace audio

#endif // MS20_FILTER_PROCESSOR_HPP
