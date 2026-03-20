/**
 * @file CvMixerProcessor.cpp
 * @brief CV mixer do_pull and self-registration.
 */
#include "CvMixerProcessor.hpp"
#include "../../core/ModuleRegistry.hpp"
#include <algorithm>

namespace audio {

CvMixerProcessor::CvMixerProcessor(int sample_rate) {
    ramp_samples_ = static_cast<int>(static_cast<float>(sample_rate) * kRampSeconds);
    declare_port({"cv_in_1", PORT_CONTROL, PortDirection::IN,  false});
    declare_port({"cv_in_2", PORT_CONTROL, PortDirection::IN,  false});
    declare_port({"cv_in_3", PORT_CONTROL, PortDirection::IN,  false});
    declare_port({"cv_in_4", PORT_CONTROL, PortDirection::IN,  false});
    declare_port({"cv_out",  PORT_CONTROL, PortDirection::OUT, false});

    declare_parameter({"gain_1", "Gain 1", -1.0f, 1.0f, 1.0f});
    declare_parameter({"gain_2", "Gain 2", -1.0f, 1.0f, 1.0f});
    declare_parameter({"gain_3", "Gain 3", -1.0f, 1.0f, 1.0f});
    declare_parameter({"gain_4", "Gain 4", -1.0f, 1.0f, 1.0f});
    declare_parameter({"offset", "DC Offset", -1.0f, 1.0f, 0.0f});
}

void CvMixerProcessor::do_pull(std::span<float> output, const VoiceContext*) {
    const int n = static_cast<int>(output.size());
    gain_[0].advance(n);
    gain_[1].advance(n);
    gain_[2].advance(n);
    gain_[3].advance(n);
    offset_.advance(n);

    for (size_t i = 0; i < output.size(); ++i) {
        float v = offset_.get();
        for (int s = 0; s < 4; ++s) {
            if (i < slots_[s].size())
                v += gain_[s].get() * slots_[s][i];
        }
        output[i] = std::clamp(v, -1.0f, 1.0f);
    }
    for (auto& s : slots_) s = {};
}

namespace {
[[maybe_unused]] const bool kRegistered = ModuleRegistry::instance().register_module(
    "CV_MIXER",
    "CV attenuverter/mixer — sums 4 control signals with independent gains and DC offset",
    "Connect LFO:control_out → CV_MIXER:cv_in_1 and ADSR:envelope_out → cv_in_2. "
    "gain_1 sets LFO depth; gain_2 * ADSR gives delayed vibrato onset. "
    "Negative gain inverts the signal; offset adds DC bias.",
    [](int sr) { return std::make_unique<CvMixerProcessor>(sr); }
);
} // namespace

} // namespace audio
