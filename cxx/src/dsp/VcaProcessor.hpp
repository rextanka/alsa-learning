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
 * Until the typed port system (Phase 14a) is in place, callers drive the VCA
 * by invoking the static apply() helper with explicit audio and CV spans.
 * The Processor pull() interface is a no-op stub.
 */

#ifndef VCA_PROCESSOR_HPP
#define VCA_PROCESSOR_HPP

#include "Processor.hpp"
#include <algorithm>
#include <span>

namespace audio {

class VcaProcessor : public Processor {
public:
    VcaProcessor() = default;

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

protected:
    // Processor stub — VcaProcessor is driven via apply(), not pull().
    // A full pull()-based implementation will be wired in Phase 14 once
    // the typed port system provides PORT_AUDIO / PORT_CONTROL connections.
    void do_pull(std::span<float> output,
                 const VoiceContext* /*ctx*/ = nullptr) override {
        std::fill(output.begin(), output.end(), 0.0f);
    }
};

} // namespace audio

#endif // VCA_PROCESSOR_HPP
