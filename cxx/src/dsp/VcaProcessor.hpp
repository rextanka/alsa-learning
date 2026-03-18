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
#include "SmoothedParam.hpp"
#include <algorithm>
#include <span>

namespace audio {

class VcaProcessor : public Processor {
public:
    static constexpr float kRampSeconds = 0.010f; // 10 ms

    explicit VcaProcessor(int sample_rate = 48000) {
        ramp_samples_ = static_cast<int>(static_cast<float>(sample_rate) * kRampSeconds);
        // Phase 15: named port declarations
        declare_port({"audio_in",        PORT_AUDIO,   PortDirection::IN});
        declare_port({"audio_out",       PORT_AUDIO,   PortDirection::OUT});
        declare_port({"gain_cv",         PORT_CONTROL, PortDirection::IN,  true}); // unipolar [0,1]
        declare_port({"initial_gain_cv", PORT_CONTROL, PortDirection::IN,  true}); // unipolar [0,1]

        // initial_gain is queryable via the registry but the initial_gain_cv port is not
        // yet wired in the graph executor. Full tremolo-with-DC-offset requires Phase 16
        // full port routing so the executor can pull initial_gain_cv and pass it as scale.
        declare_parameter({"initial_gain",   "Initial Gain",   0.0f, 1.0f, 1.0f});
        declare_parameter({"response_curve", "Response Curve", 0.0f, 1.0f, 0.0f});
    }

    bool apply_parameter(const std::string& name, float value) override {
        if (name == "initial_gain")   { initial_gain_.set_target(std::clamp(value, 0.0f, 1.0f), ramp_samples_); return true; }
        if (name == "response_curve") { response_curve_ = std::clamp(value, 0.0f, 1.0f); return true; }
        return false;
    }

    /**
     * @brief Apply VCA gain: audio[i] *= gain_cv[i] * scale.
     *
     * RT-SAFE: no allocations, no locks, no branches per sample.
     *
     * @param audio    Audio buffer to scale in-place (PORT_AUDIO).
     * @param gain_cv  Per-sample gain envelope (PORT_CONTROL, range [0,1]).
     * @param scale    Static amplitude scale — base_amplitude or initial_gain.
     *                 Defaults to 1.0 (unity).
     */
    static void apply(std::span<float> audio,
                      std::span<const float> gain_cv,
                      float scale = 1.0f) noexcept {
        // RT-SAFE: no allocations, no locks
        const size_t n = std::min(audio.size(), gain_cv.size());
        for (size_t i = 0; i < n; ++i) {
            audio[i] *= gain_cv[i] * scale;
        }
    }

    void reset() override {}

    float initial_gain()   const { return initial_gain_.get(); }
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
    int ramp_samples_             = 480; // default ~10ms at 48kHz
    SmoothedParam initial_gain_{1.0f};
    float response_curve_ = 0.0f; // 0=linear, 1=exponential (Phase 17)
};

} // namespace audio

#endif // VCA_PROCESSOR_HPP
