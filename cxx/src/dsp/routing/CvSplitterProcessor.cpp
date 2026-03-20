/**
 * @file CvSplitterProcessor.cpp
 * @brief CV splitter do_pull and self-registration.
 */
#include "CvSplitterProcessor.hpp"
#include "../../core/ModuleRegistry.hpp"
#include <algorithm>

namespace audio {

CvSplitterProcessor::CvSplitterProcessor(int sample_rate) {
    ramp_samples_ = static_cast<int>(static_cast<float>(sample_rate) * kRampSeconds);
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

void CvSplitterProcessor::do_pull(std::span<float> output, const VoiceContext*) {
    const int n_frames = static_cast<int>(output.size());
    gain_[0].advance(n_frames);
    gain_[1].advance(n_frames);
    gain_[2].advance(n_frames);
    gain_[3].advance(n_frames);

    if (!injected_.empty()) {
        size_t n = std::min(output.size(), injected_.size());
        const float g0 = gain_[0].get();
        for (size_t i = 0; i < n; ++i)
            output[i] = std::clamp(g0 * injected_[i], -2.0f, 2.0f);
        for (size_t i = n; i < output.size(); ++i) output[i] = 0.0f;
    } else {
        for (auto& s : output) s = 0.0f;
    }
    injected_ = {};
}

namespace {
[[maybe_unused]] const bool kRegistered = ModuleRegistry::instance().register_module(
    "CV_SPLITTER",
    "CV 1-to-4 fan-out — distributes one control signal to multiple destinations",
    "Connect ADSR:envelope_out → CV_SPLITTER:cv_in. "
    "cv_out_1 → VCA:gain_cv (full depth); cv_out_2 → VCF:cutoff_cv (scaled by gain_2). "
    "Use CV_MIXER for fully independent per-branch gains.",
    [](int sr) { return std::make_unique<CvSplitterProcessor>(sr); }
);
} // namespace

} // namespace audio
