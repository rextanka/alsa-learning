/**
 * @file VcaProcessor.hpp
 * @brief Voltage Controlled Amplifier — multiplies audio by a gain CV signal.
 *
 * Implements MODULE_DESC §3 VCA contract:
 *   PORT_AUDIO   in:  audio_in
 *   PORT_AUDIO   out: audio_out
 *   PORT_CONTROL in:  gain_cv, initial_gain_cv
 *
 * The VCA and Envelope Generator are distinct nodes. The Envelope Generator
 * produces a PORT_CONTROL signal (filled into a dedicated buffer via pull())
 * which is passed to VcaProcessor::apply() as the gain_cv argument.
 *
 * The graph executor handles VCA as a special case: it resolves the gain_cv
 * connection and calls the static apply() helper directly. do_pull() is unused.
 */

#ifndef VCA_PROCESSOR_HPP
#define VCA_PROCESSOR_HPP

#include "Processor.hpp"
#include <algorithm>
#include <cmath>
#include <span>

namespace audio {

class VcaProcessor : public Processor {
public:
    /**
     * Exponential dynamic range: ln(10^(60/20)) = ln(1000) ≈ 6.908.
     * M-130 spec: 10dB/oct. 60dB total range over the normalised [0,1] CV span
     * gives a tight, punchy characteristic matching the Roland hardware.
     * At g=1 → exp(0)=1.0 (unity); at g=0 → exp(-6.908)≈0.001 (-60dB ≈ silence).
     */
    static constexpr float kLogRange = 6.908f;

    explicit VcaProcessor(int /*sample_rate*/ = 48000) {
        // Phase 15: named port declarations
        declare_port({"audio_in",        PORT_AUDIO,   PortDirection::IN});
        declare_port({"audio_out",       PORT_AUDIO,   PortDirection::OUT});
        declare_port({"gain_cv",         PORT_CONTROL, PortDirection::IN,  true}); // unipolar [0,1]
        declare_port({"initial_gain_cv", PORT_CONTROL, PortDirection::IN,  true}); // unipolar [0,1]

        // initial_gain_cv is wired in Voice.cpp VCA block (Phase 26).
        // When connected: scale = initial_gain_cv[0] * base_amplitude_.
        declare_parameter({"initial_gain",   "Initial Gain",   0.0f, 1.0f, 1.0f});
        declare_parameter({"response_curve", "Response Curve", 0.0f, 1.0f, 0.0f});
    }

    bool apply_parameter(const std::string& name, float value) override {
        if (name == "initial_gain")   { initial_gain_ = std::clamp(value, 0.0f, 1.0f); return true; }
        if (name == "response_curve") { response_curve_ = std::clamp(value, 0.0f, 1.0f); return true; }
        return false;
    }

    /**
     * @brief Apply VCA gain: audio[i] *= effective_gain[i] * scale.
     *
     * RT-SAFE: no allocations, no locks, no branches per sample.
     *
     * @param audio          Audio buffer to scale in-place (PORT_AUDIO).
     * @param gain_cv        Per-sample gain envelope (PORT_CONTROL, range [0,1]).
     * @param scale          Static amplitude scale — initial_gain * base_amplitude_.
     *                       Defaults to 1.0 (unity).
     * @param response_curve Blend between linear (0.0) and exponential (1.0) law.
     *                       Exponential follows M-130 spec (10dB/oct, 60dB range):
     *                       exp_gain = exp((g-1)*kLogRange), normalised so g=1→1.0.
     *                       Defaults to 0.0 (linear, backward-compatible).
     */
    static void apply(std::span<float> audio,
                      std::span<const float> gain_cv,
                      float scale = 1.0f,
                      float response_curve = 0.0f) noexcept {
        const size_t n = std::min(audio.size(), gain_cv.size());
        if (response_curve < 1e-4f) {
            // Fast path: linear (default)
            for (size_t i = 0; i < n; ++i)
                audio[i] *= gain_cv[i] * scale;
        } else {
            // Exponential path: M-130 10dB/oct characteristic.
            // exp_gain = exp((g-1)*kLogRange), blend with linear via response_curve.
            for (size_t i = 0; i < n; ++i) {
                const float g     = gain_cv[i];
                const float g_exp = (g > 0.0f) ? std::exp((g - 1.0f) * kLogRange) : 0.0f;
                audio[i] *= (g + response_curve * (g_exp - g)) * scale;
            }
        }
    }

    void reset() override {}

    float initial_gain()   const { return initial_gain_; }
    float response_curve() const { return response_curve_; }

    // VcaProcessor consumes a PORT_CONTROL gain signal (from AdsrEnvelopeProcessor).
    PortType input_port_type() const override { return PortType::PORT_CONTROL; }

protected:
    // VcaProcessor is driven via the static apply() helper, not pull().
    // The pull() stub is present to satisfy the Processor interface.
    void do_pull(std::span<float> output,
                 const VoiceContext* /*ctx*/ = nullptr) override {
        std::fill(output.begin(), output.end(), 0.0f);
    }

private:
    float initial_gain_  = 1.0f; // multiplicative scale on base_amplitude_ (velocity/level)
    float response_curve_ = 0.0f; // 0=linear, 1=true exponential (M-130 10dB/oct)
};

} // namespace audio

#endif // VCA_PROCESSOR_HPP
