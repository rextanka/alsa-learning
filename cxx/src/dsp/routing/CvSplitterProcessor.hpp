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
 * For now this node outputs: input * gain_1. Use CV_MIXER when independent gains
 * per branch are needed (one CV_MIXER per branch, each receiving the same source).
 *
 * Usage — ADSR to both VCA and VCF:
 *   ADSR:envelope_out → CV_SPLITTER:cv_in
 *   CV_SPLITTER:cv_out_1 → VCA:gain_cv       (full depth)
 *   CV_SPLITTER:cv_out_2 → VCF:cutoff_cv     (gain_2 scales filter mod depth)
 */

#ifndef CV_SPLITTER_PROCESSOR_HPP
#define CV_SPLITTER_PROCESSOR_HPP

#include "../Processor.hpp"
#include <algorithm>

namespace audio {

class CvSplitterProcessor : public Processor {
public:
    CvSplitterProcessor() {
        declare_port({"cv_in",    PORT_CONTROL, PortDirection::IN,  false});
        declare_port({"cv_out_1", PORT_CONTROL, PortDirection::OUT, false});
        declare_port({"cv_out_2", PORT_CONTROL, PortDirection::OUT, false});
        declare_port({"cv_out_3", PORT_CONTROL, PortDirection::OUT, false});
        declare_port({"cv_out_4", PORT_CONTROL, PortDirection::OUT, false});

        declare_parameter({"gain_1", "Gain 1", -2.0f, 2.0f, 1.0f});
        declare_parameter({"gain_2", "Gain 2", -2.0f, 2.0f, 1.0f});
        declare_parameter({"gain_3", "Gain 3", -2.0f, 2.0f, 1.0f});
        declare_parameter({"gain_4", "Gain 4", -2.0f, 2.0f, 1.0f});
    }

    PortType output_port_type() const override { return PortType::PORT_CONTROL; }

    void reset() override { injected_ = {}; }

    bool apply_parameter(const std::string& name, float value) override {
        if (name == "gain_1") { gain_[0] = value; return true; }
        if (name == "gain_2") { gain_[1] = value; return true; }
        if (name == "gain_3") { gain_[2] = value; return true; }
        if (name == "gain_4") { gain_[3] = value; return true; }
        return false;
    }

    void inject_cv(std::string_view port_name, std::span<const float> cv) override {
        if (port_name == "cv_in") injected_ = cv;
    }

protected:
    void do_pull(std::span<float> output,
                 const VoiceContext* /*ctx*/ = nullptr) override {
        // Output = input * gain_1. All cv_out_N connections read this same buffer.
        if (!injected_.empty()) {
            size_t n = std::min(output.size(), injected_.size());
            for (size_t i = 0; i < n; ++i)
                output[i] = std::clamp(gain_[0] * injected_[i], -2.0f, 2.0f);
            for (size_t i = n; i < output.size(); ++i) output[i] = 0.0f;
        } else {
            for (auto& s : output) s = 0.0f;
        }
        injected_ = {};
    }

private:
    float gain_[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    std::span<const float> injected_;
};

} // namespace audio

#endif // CV_SPLITTER_PROCESSOR_HPP
