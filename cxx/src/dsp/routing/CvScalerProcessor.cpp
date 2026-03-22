/**
 * @file CvScalerProcessor.cpp
 * @brief CV scaler/attenuverter do_pull and self-registration.
 */
#include "CvScalerProcessor.hpp"
#include "../../core/ModuleRegistry.hpp"

namespace audio {

void CvScalerProcessor::do_pull(std::span<float> output, const VoiceContext*) {
    scale_.advance(static_cast<int>(output.size()));
    offset_.advance(static_cast<int>(output.size()));
    const float scale_val  = scale_.get();
    const float offset_val = offset_.get();
    if (!injected_.empty()) {
        const size_t n = std::min(output.size(), injected_.size());
        for (size_t i = 0; i < n; ++i)
            output[i] = scale_val * injected_[i] + offset_val;
        for (size_t i = n; i < output.size(); ++i)
            output[i] = offset_val;
    } else {
        for (auto& s : output) s = offset_val;
    }
    injected_ = {};
}

namespace {
[[maybe_unused]] const bool kRegistered = ModuleRegistry::instance().register_module(
    "CV_SCALER",
    "CV amplifier/attenuverter — scales a control signal by an arbitrary factor",
    "Route ADSR:envelope_out → CV_SCALER:cv_in → VCF:cutoff_cv with scale=4.0 "
    "to get 4 octaves of filter sweep from a 0–1 envelope. "
    "offset shifts the resting CV (e.g. offset=-2.0 centres a bipolar sweep). "
    "scale=-1.0 is equivalent to INVERTER. "
    "Place CV_SCALER before its downstream target in the chain list.",
    [](int sr) { return std::make_unique<CvScalerProcessor>(sr); }
);
} // namespace

} // namespace audio
