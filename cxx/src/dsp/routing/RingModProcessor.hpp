/**
 * @file RingModProcessor.hpp
 * @brief Ring modulator — 4-quadrant multiplication of two audio signals.
 *
 * RT-SAFE chain node: PORT_AUDIO (audio_in_a × audio_in_b) → PORT_AUDIO out.
 * Type name: RING_MOD
 *
 * Implements output[n] = A[n] × B[n], suppressing both carrier frequencies
 * and producing only sum (A+B) and difference (A−B) sidebands. When A and B
 * are pitched VCOs at an inharmonic interval, the result is a bell-like or
 * metallic timbre inharmonic to either input.
 *
 * Multi-input execution: both audio_in_a and audio_in_b are injected via
 * inject_audio() by Voice::pull_mono before do_pull() is called. do_pull()
 * ignores the `output` content on entry and writes A×B to it.
 *
 * If only audio_in_a is injected (no audio_in_b), the node self-squares
 * (sin²(ωt) = ½ − ½·cos(2ωt)) — which yields a doubled-frequency partial.
 *
 * Optional mod_in (PORT_CONTROL): an LFO or slow CV multiplied directly with
 * audio_in_a at control rate, enabling bowed tremolo at LFO rates.
 *
 * Mix parameter (0–1): 0 = dry (passthrough audio_in_a), 1 = fully wet ring-mod.
 *
 * Reference: Roland Practical Synthesis Vol 1 §2-5, Fig 2-19 (Violin Tremolo);
 *            Vol 1 §2-4, Fig 2-12 (Bell/Gong).
 */

#ifndef RING_MOD_PROCESSOR_HPP
#define RING_MOD_PROCESSOR_HPP

#include "../Processor.hpp"
#include "../SmoothedParam.hpp"
#include <algorithm>

namespace audio {

class RingModProcessor : public Processor {
public:
    static constexpr float kRampSeconds = 0.010f; // 10 ms

    explicit RingModProcessor(int sample_rate = 48000);

    void reset() override {
        audio_in_a_ = {};
        audio_in_b_ = {};
        mod_cv_     = 0.0f;
    }

    bool apply_parameter(const std::string& name, float value) override {
        if (name == "mix") { mix_.set_target(std::clamp(value, 0.0f, 1.0f), ramp_samples_); return true; }
        return false;
    }

    void inject_audio(std::string_view port_name,
                      std::span<const float> audio) override {
        if (port_name == "audio_in_a") audio_in_a_ = audio;
        else if (port_name == "audio_in_b") audio_in_b_ = audio;
    }

    void inject_cv(std::string_view port_name,
                   std::span<const float> cv) override {
        if (port_name == "mod_in" && !cv.empty()) {
            float sum = 0.0f;
            for (auto v : cv) sum += v;
            mod_cv_ = sum / static_cast<float>(cv.size());
        }
    }

    PortType output_port_type() const override { return PortType::PORT_AUDIO; }

protected:
    void do_pull(std::span<float> output, const VoiceContext* ctx = nullptr) override;

private:
    int ramp_samples_ = 480;
    std::span<const float> audio_in_a_;
    std::span<const float> audio_in_b_;
    float mod_cv_  = 0.0f;
    SmoothedParam mix_{1.0f};
};

} // namespace audio

#endif // RING_MOD_PROCESSOR_HPP
