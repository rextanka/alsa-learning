/**
 * @file VcfBase.hpp
 * @brief Abstract base class for all VCF (voltage-controlled filter) processors.
 *
 * Consolidates the shared state, port declarations, parameter handling, and
 * do_pull implementations that are common to all four filter types:
 *   MOOG_FILTER, DIODE_FILTER, SH_FILTER, MS20_FILTER
 *
 * Subclasses implement three pure virtuals:
 *   update_cutoff_coefficient(float effective_cutoff)
 *   process_sample(float& sample)
 *   reset_state()
 *
 * And may override the virtual hook:
 *   on_resonance_changed()  — called when resonance or res_cv changes (default: no-op)
 *
 * Common port declarations (declared in VcfBase constructor):
 *   audio_in   PORT_AUDIO   IN
 *   audio_out  PORT_AUDIO   OUT
 *   cutoff_cv  PORT_CONTROL IN  bipolar (1V/oct → LP cutoff)
 *   res_cv     PORT_CONTROL IN  unipolar (additive resonance)
 *   kybd_cv    PORT_CONTROL IN  bipolar (1V/oct keyboard tracking, auto-injected by Voice)
 *
 * Common parameters (declared in VcfBase constructor):
 *   cutoff    (20–20000 Hz, log)
 *   resonance (0.0–1.0)
 */

#ifndef VCF_BASE_HPP
#define VCF_BASE_HPP

#include "../Processor.hpp"
#include "../SmoothedParam.hpp"
#include <algorithm>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace audio {

class VcfBase : public Processor {
public:
    explicit VcfBase(int sample_rate)
        : sample_rate_(sample_rate)
        , ramp_samples_(static_cast<int>(static_cast<float>(sample_rate) * kRampSeconds))
    {
        declare_port({"audio_in",  PORT_AUDIO,   PortDirection::IN});
        declare_port({"audio_out", PORT_AUDIO,   PortDirection::OUT});
        declare_port({"cutoff_cv", PORT_CONTROL, PortDirection::IN, false}); // bipolar 1V/oct
        declare_port({"res_cv",    PORT_CONTROL, PortDirection::IN, true});  // unipolar additive
        declare_port({"kybd_cv",   PORT_CONTROL, PortDirection::IN, false}); // bipolar 1V/oct
        // fm_in declared in LadderVcfBase for MOOG_FILTER and DIODE_FILTER only

        declare_parameter({"cutoff",    "Cutoff Frequency", 20.0f, 20000.0f, 20000.0f, true});
        declare_parameter({"resonance", "Resonance",         0.0f,     1.0f,     0.0f});
    }

    bool apply_parameter(const std::string& name, float value) override {
        if (name == "cutoff") {
            const float clamp = std::clamp(value, 20.0f, static_cast<float>(sample_rate_) * 0.45f);
            base_cutoff_.set_target(clamp, ramp_samples_);
            update_effective_cutoff();
            return true;
        }
        if (name == "resonance" || name == "res") {
            const float clamp = std::clamp(value, 0.0f, 1.0f);
            base_res_.set_target(clamp, ramp_samples_);
            res_ = base_res_.get();
            on_resonance_changed();
            return true;
        }
        if (name == "cutoff_cv") {
            cutoff_cv_ = value;
            update_effective_cutoff();
            return true;
        }
        if (name == "kybd_cv") {
            kybd_cv_ = value;
            update_effective_cutoff();
            return true;
        }
        if (name == "res_cv") {
            res_cv_accum_ = value;
            res_ = std::clamp(base_res_.get() + res_cv_accum_, 0.0f, 1.0f);
            on_resonance_changed();
            return true;
        }
        return false;
    }

    void reset() override { reset_state(); }

protected:
    // -------------------------------------------------------------------
    // Pure virtuals — each concrete filter must implement these
    // -------------------------------------------------------------------

    /** Update internal frequency coefficient(s) from the effective cutoff (Hz). */
    virtual void update_cutoff_coefficient(float effective_cutoff) = 0;

    /** Process one sample in-place (reads input, writes output). */
    virtual void process_sample(float& sample) = 0;

    /** Zero all filter state registers (called on voice reset and from reset()). */
    virtual void reset_state() = 0;

    // -------------------------------------------------------------------
    // Virtual hook — override to recompute coefficients on resonance change
    // -------------------------------------------------------------------

    virtual void on_resonance_changed() {}

    // -------------------------------------------------------------------
    // Shared helpers
    // -------------------------------------------------------------------

    void update_effective_cutoff() {
        // Guard: skip the std::pow() call when neither the base cutoff nor the
        // combined CV sum has changed since the last coefficient update.
        const float cv_sum = cutoff_cv_ + kybd_cv_;
        const float cur_cutoff = base_cutoff_.get();
        if (cur_cutoff == last_base_cutoff_ && cv_sum == last_cv_sum_) return;
        last_base_cutoff_ = cur_cutoff;
        last_cv_sum_      = cv_sum;
        // LFO CV and keyboard tracking are additive in the log domain (both 1V/oct).
        float eff = std::max(20.0f, cur_cutoff * std::pow(2.0f, cv_sum));
        update_cutoff_coefficient(eff);
    }

    // -------------------------------------------------------------------
    // do_pull implementations (shared by all subclasses)
    // -------------------------------------------------------------------

    void do_pull(std::span<float> output,
                 const VoiceContext* /*ctx*/ = nullptr) override {
        const int n = static_cast<int>(output.size());
        if (base_cutoff_.is_ramping() || base_res_.is_ramping()) {
            base_cutoff_.advance(n);
            base_res_.advance(n);
            last_base_cutoff_ = -1.f; // force recompute
            update_effective_cutoff();
            res_ = std::clamp(base_res_.get() + res_cv_accum_, 0.0f, 1.0f);
            on_resonance_changed();
        } else {
            base_cutoff_.advance(n); // no-op when not ramping
            base_res_.advance(n);
        }
        for (auto& s : output) process_sample(s);
    }

    void do_pull(AudioBuffer& output,
                 const VoiceContext* /*ctx*/ = nullptr) override {
        const int n = static_cast<int>(output.frames());
        if (base_cutoff_.is_ramping() || base_res_.is_ramping()) {
            base_cutoff_.advance(n);
            base_res_.advance(n);
            last_base_cutoff_ = -1.f;
            update_effective_cutoff();
            res_ = std::clamp(base_res_.get() + res_cv_accum_, 0.0f, 1.0f);
            on_resonance_changed();
        } else {
            base_cutoff_.advance(n);
            base_res_.advance(n);
        }
        for (size_t i = 0; i < output.frames(); ++i) {
            float s = (output.left[i] + output.right[i]) * 0.5f;
            process_sample(s);
            output.left[i] = output.right[i] = s;
        }
    }

    // -------------------------------------------------------------------
    // Shared state — accessible to all subclasses
    // -------------------------------------------------------------------

    static constexpr float kRampSeconds = 0.010f; // 10 ms

    int   sample_rate_;
    int   ramp_samples_;
    SmoothedParam base_cutoff_{20000.0f}; ///< anchor cutoff (Hz), set by "cutoff" parameter
    float cutoff_cv_   = 0.0f;     ///< LFO/envelope CV (1V/oct, bipolar)
    float kybd_cv_     = 0.0f;     ///< keyboard tracking CV (1V/oct, auto-injected by Voice)
    SmoothedParam base_res_{0.0f}; ///< base resonance, set by "resonance" parameter
    float res_         = 0.0f;     ///< effective resonance (base + res_cv)
    float res_cv_accum_ = 0.0f;   ///< last res_cv value received via apply_parameter

private:
    // Cache for update_effective_cutoff() — avoids std::pow() when inputs unchanged.
    // Initialised to -1 (impossible cutoff) to force computation on first call.
    float last_base_cutoff_ = -1.0f;
    float last_cv_sum_      = 0.0f;
};

} // namespace audio

#endif // VCF_BASE_HPP
