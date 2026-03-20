/**
 * @file CvSplitterProcessor.hpp
 * @brief CV splitter — fans one control signal to up to 4 destinations.
 *
 * Type name: CV_SPLITTER
 *
 * Ports (PORT_CONTROL):
 *   cv_in              IN  bipolar [-1,1]
 *   cv_out_1 … cv_out_4  OUT bipolar [-1,1]
 *
 * Parameters:
 *   gain_1 … gain_4  (-2.0 – 2.0, default 1.0) — per-output scale
 *
 * Architecture note: the Voice executor produces ONE output buffer per mod_source.
 * All connections from this node's output ports (cv_out_1 … cv_out_4) read the
 * same buffer. Per-output independent gains require a future executor extension.
 * For now this node outputs: input * gain_1.
 *
 * Usage — ADSR to both VCA and VCF:
 *   ADSR:envelope_out → CV_SPLITTER:cv_in
 *   CV_SPLITTER:cv_out_1 → VCA:gain_cv       (full depth)
 *   CV_SPLITTER:cv_out_2 → VCF:cutoff_cv     (gain_2 scales filter mod depth)
 */

#ifndef CV_SPLITTER_PROCESSOR_HPP
#define CV_SPLITTER_PROCESSOR_HPP

#include "../Processor.hpp"
#include "../SmoothedParam.hpp"
#include <algorithm>

namespace audio {

class CvSplitterProcessor : public Processor {
public:
    static constexpr float kRampSeconds = 0.010f;

    explicit CvSplitterProcessor(int sample_rate = 48000);

    PortType output_port_type() const override { return PortType::PORT_CONTROL; }

    void reset() override { injected_ = {}; }

    bool apply_parameter(const std::string& name, float value) override {
        // CV routing gains snap immediately — patch-configuration values.
        if (name == "gain_1") { gain_[0].set_target(value, 0); return true; }
        if (name == "gain_2") { gain_[1].set_target(value, 0); return true; }
        if (name == "gain_3") { gain_[2].set_target(value, 0); return true; }
        if (name == "gain_4") { gain_[3].set_target(value, 0); return true; }
        return false;
    }

    void inject_cv(std::string_view port_name, std::span<const float> cv) override {
        if (port_name == "cv_in") injected_ = cv;
    }

protected:
    void do_pull(std::span<float> output, const VoiceContext* ctx = nullptr) override;

private:
    int ramp_samples_ = 480;
    SmoothedParam gain_[4] = {SmoothedParam{1.0f}, SmoothedParam{1.0f}, SmoothedParam{1.0f}, SmoothedParam{1.0f}};
    std::span<const float> injected_;
};

} // namespace audio

#endif // CV_SPLITTER_PROCESSOR_HPP
