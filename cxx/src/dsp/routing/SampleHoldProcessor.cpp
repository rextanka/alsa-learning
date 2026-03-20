/**
 * @file SampleHoldProcessor.cpp
 * @brief Sample & Hold do_pull and self-registration.
 */
#include "SampleHoldProcessor.hpp"
#include "../../core/ModuleRegistry.hpp"

namespace audio {

SampleHoldProcessor::SampleHoldProcessor() {
    declare_port({"cv_in",    PORT_CONTROL, PortDirection::IN,  false});
    declare_port({"clock_in", PORT_CONTROL, PortDirection::IN,  true});
    declare_port({"cv_out",   PORT_CONTROL, PortDirection::OUT, false});
}

void SampleHoldProcessor::do_pull(std::span<float> output, const VoiceContext*) {
    for (size_t i = 0; i < output.size(); ++i) {
        float clk = clk_injected_.empty() ? 0.0f
                  : (i < clk_injected_.size() ? clk_injected_[i] : clk_injected_.back());
        float src = cv_injected_.empty()  ? 0.0f
                  : (i < cv_injected_.size()  ? cv_injected_[i]  : cv_injected_.back());

        const bool high = (clk > 0.5f);
        if (high && !prev_clock_) held_value_ = src;
        prev_clock_ = high;
        output[i]   = held_value_;
    }
    cv_injected_  = {};
    clk_injected_ = {};
}

namespace {
[[maybe_unused]] const bool kRegistered = ModuleRegistry::instance().register_module(
    "SAMPLE_HOLD",
    "Sample & Hold — captures cv_in on each rising clock edge and holds until next",
    "Connect WHITE_NOISE:audio_out → SAMPLE_HOLD:cv_in, LFO(square):control_out → clock_in. "
    "cv_out → VCO:pitch_cv gives random stepped pitch. "
    "Use LFO saw → cv_in with LFO2 square clock for deterministic staircase ramps.",
    [](int /*sr*/) { return std::make_unique<SampleHoldProcessor>(); }
);
} // namespace

} // namespace audio
