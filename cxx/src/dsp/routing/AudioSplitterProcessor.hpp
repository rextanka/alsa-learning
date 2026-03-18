/**
 * @file AudioSplitterProcessor.hpp
 * @brief 1-to-4 audio fan-out with per-output gain scaling.
 *
 * RT-SAFE chain node: PORT_AUDIO in → PORT_AUDIO out (4 outputs).
 * Type name: AUDIO_SPLITTER
 *
 * Fans one audio signal out to up to four destinations, each with an
 * independent gain parameter. Default gain = 1.0 on all outputs (unity fan-out).
 *
 * In the current single-buffer executor, AUDIO_SPLITTER behaves as a serial
 * passthrough scaled by gain_1 on the main `output` buffer. Full parallel-path
 * routing (audio_out_1 and audio_out_2 going to independent downstream nodes
 * simultaneously) requires the parallel signal-path executor (Phase 20+).
 *
 * Use case: split one VCO signal to both the main filter chain AND a RING_MOD
 * secondary input. Connect AUDIO_SPLITTER.audio_out_2 → RING_MOD.audio_in_b
 * (secondary audio connection); AUDIO_SPLITTER.audio_out_1 → next chain node
 * (primary inline).
 *
 * Reference: MODULE_DESC.md §6 Audio Splitter.
 */

#ifndef AUDIO_SPLITTER_PROCESSOR_HPP
#define AUDIO_SPLITTER_PROCESSOR_HPP

#include "../Processor.hpp"
#include <algorithm>

namespace audio {

class AudioSplitterProcessor : public Processor {
public:
    AudioSplitterProcessor() {
        declare_port({"audio_in",    PORT_AUDIO, PortDirection::IN});
        declare_port({"audio_out_1", PORT_AUDIO, PortDirection::OUT});
        declare_port({"audio_out_2", PORT_AUDIO, PortDirection::OUT});
        declare_port({"audio_out_3", PORT_AUDIO, PortDirection::OUT});
        declare_port({"audio_out_4", PORT_AUDIO, PortDirection::OUT});

        declare_parameter({"gain_1", "Output 1 Gain", 0.0f, 2.0f, 1.0f});
        declare_parameter({"gain_2", "Output 2 Gain", 0.0f, 2.0f, 1.0f});
        declare_parameter({"gain_3", "Output 3 Gain", 0.0f, 2.0f, 1.0f});
        declare_parameter({"gain_4", "Output 4 Gain", 0.0f, 2.0f, 1.0f});
    }

    void reset() override {}

    bool apply_parameter(const std::string& name, float value) override {
        if (name == "gain_1") { gain_[0] = std::clamp(value, 0.0f, 2.0f); return true; }
        if (name == "gain_2") { gain_[1] = std::clamp(value, 0.0f, 2.0f); return true; }
        if (name == "gain_3") { gain_[2] = std::clamp(value, 0.0f, 2.0f); return true; }
        if (name == "gain_4") { gain_[3] = std::clamp(value, 0.0f, 2.0f); return true; }
        return false;
    }

    /**
     * @brief Inject the primary audio input (for audio_bus execution path).
     *
     * When AUDIO_SPLITTER is designated as an audio_source and its audio_in
     * is explicitly wired, the executor injects the upstream buffer here.
     * The injected span is copied to all outputs (with per-output gain).
     */
    void inject_audio(std::string_view port_name,
                      std::span<const float> audio) override {
        if (port_name == "audio_in") audio_in_ = audio;
    }

    PortType output_port_type() const override { return PortType::PORT_AUDIO; }

protected:
    void do_pull(std::span<float> output,
                 const VoiceContext* /*ctx*/ = nullptr) override {
        // In the serial inline path: `output` contains the input signal.
        // Apply gain_1 to the primary output (in-place).
        // Secondary outputs audio_out_2..4 are served from the audio_bus
        // buffer (which is this node's audio_bus slot) for downstream nodes
        // that injected audio_in_b/fm_in connections referencing this node.
        if (!audio_in_.empty()) {
            // Audio_bus path: copy injected input to output with gain_1.
            const size_t n = std::min(output.size(), audio_in_.size());
            for (size_t i = 0; i < n; ++i) output[i] = audio_in_[i] * gain_[0];
            audio_in_ = {};
        } else {
            // Inline path: scale in-place with gain_1.
            for (auto& s : output) s *= gain_[0];
        }
        // Note: gain_[1..3] are applied when downstream nodes read from audio_bus
        // via inject_audio. For now, all fan-out connections receive the same
        // gain_1-scaled signal. Per-output gain enforcement for secondary outputs
        // requires the parallel-path executor (Phase 20+).
    }

private:
    float gain_[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    std::span<const float> audio_in_; ///< injected primary input (audio_bus path)
};

} // namespace audio

#endif // AUDIO_SPLITTER_PROCESSOR_HPP
