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
    explicit VcfBase(int sample_rate);

    bool apply_parameter(const std::string& name, float value) override;
    void reset() override { reset_state(); }
    bool reset_on_note_on() const override { return false; }

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
    // Shared helpers — implemented in VcfBase.cpp
    // -------------------------------------------------------------------

    void update_effective_cutoff();
    void do_pull(std::span<float> output, const VoiceContext* ctx = nullptr) override;
    void do_pull(AudioBuffer& output, const VoiceContext* ctx = nullptr) override;

    // -------------------------------------------------------------------
    // Shared state — accessible to all subclasses
    // -------------------------------------------------------------------

    static constexpr float kRampSeconds = 0.010f; // 10 ms

    int   sample_rate_;
    int   ramp_samples_;
    SmoothedParam base_cutoff_{20000.0f}; ///< anchor cutoff (Hz), set by "cutoff" parameter
    float cutoff_cv_    = 0.0f;     ///< LFO/envelope CV (1V/oct, bipolar)
    float kybd_cv_      = 0.0f;     ///< keyboard tracking CV (1V/oct, auto-injected by Voice)
    SmoothedParam base_res_{0.0f};  ///< base resonance, set by "resonance" parameter
    float res_          = 0.0f;     ///< effective resonance (base + res_cv)
    float res_cv_accum_ = 0.0f;    ///< last res_cv value received via apply_parameter

private:
    // Cache for update_effective_cutoff() — avoids std::pow() when inputs unchanged.
    float last_base_cutoff_ = -1.0f;
    float last_cv_sum_      = 0.0f;
};

} // namespace audio

#endif // VCF_BASE_HPP
