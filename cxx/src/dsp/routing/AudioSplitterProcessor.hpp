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
#include "../SmoothedParam.hpp"
#include <algorithm>

namespace audio {

class AudioSplitterProcessor : public Processor {
public:
    static constexpr float kRampSeconds = 0.010f;

    explicit AudioSplitterProcessor(int sample_rate = 48000);

    void reset() override {}

    bool apply_parameter(const std::string& name, float value) override {
        if (name == "gain_1") { gain_[0].set_target(std::clamp(value, 0.0f, 2.0f), ramp_samples_); return true; }
        if (name == "gain_2") { gain_[1].set_target(std::clamp(value, 0.0f, 2.0f), ramp_samples_); return true; }
        if (name == "gain_3") { gain_[2].set_target(std::clamp(value, 0.0f, 2.0f), ramp_samples_); return true; }
        if (name == "gain_4") { gain_[3].set_target(std::clamp(value, 0.0f, 2.0f), ramp_samples_); return true; }
        return false;
    }

    void inject_audio(std::string_view port_name,
                      std::span<const float> audio) override {
        if (port_name == "audio_in") audio_in_ = audio;
    }

    PortType output_port_type() const override { return PortType::PORT_AUDIO; }

protected:
    void do_pull(std::span<float> output, const VoiceContext* ctx = nullptr) override;

private:
    int ramp_samples_ = 480;
    SmoothedParam gain_[4] = {SmoothedParam{1.0f}, SmoothedParam{1.0f}, SmoothedParam{1.0f}, SmoothedParam{1.0f}};
    std::span<const float> audio_in_;
};

} // namespace audio

#endif // AUDIO_SPLITTER_PROCESSOR_HPP
