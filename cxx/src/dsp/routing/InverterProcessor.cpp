/**
 * @file InverterProcessor.cpp
 * @brief CV inverter/scaler do_pull and self-registration.
 */
#include "InverterProcessor.hpp"
#include "../../core/ModuleRegistry.hpp"

namespace audio {

InverterProcessor::InverterProcessor(int sample_rate) {
    ramp_samples_ = static_cast<int>(static_cast<float>(sample_rate) * kRampSeconds);
    declare_port({"cv_in",  PORT_CONTROL, PortDirection::IN,  false});
    declare_port({"cv_out", PORT_CONTROL, PortDirection::OUT, false});
    declare_parameter({"scale", "Scale", -2.0f, 2.0f, -1.0f});
}

void InverterProcessor::do_pull(std::span<float> output, const VoiceContext*) {
    scale_.advance(static_cast<int>(output.size()));
    const float scale_val = scale_.get();
    if (!injected_.empty()) {
        size_t n = std::min(output.size(), injected_.size());
        for (size_t i = 0; i < n; ++i) output[i] = scale_val * injected_[i];
        for (size_t i = n; i < output.size(); ++i) output[i] = 0.0f;
    } else {
        for (auto& s : output) s = 0.0f;
    }
    injected_ = {};
}

namespace {
[[maybe_unused]] const bool kRegistered = ModuleRegistry::instance().register_module(
    "INVERTER",
    "CV inverter/scaler — multiplies a control signal by a scale factor",
    "Connect ADSR:envelope_out → INVERTER:cv_in → VCF:cutoff_cv with scale=-1.0 "
    "for a closing-filter pluck (harpsichord). scale=0.5 attenuates by half. "
    "scale=2.0 doubles the CV range for extreme modulation.",
    [](int sr) { return std::make_unique<InverterProcessor>(sr); }
);
} // namespace

} // namespace audio
