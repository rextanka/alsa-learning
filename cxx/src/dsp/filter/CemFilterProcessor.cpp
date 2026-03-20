/**
 * @file CemFilterProcessor.cpp
 * @brief CEM/SH-101 ladder process_sample and self-registration.
 */
#include "CemFilterProcessor.hpp"
#include "../../core/ModuleRegistry.hpp"
#include <cmath>

namespace audio {

void CemFilterProcessor::process_sample(float& sample) {
    // Resonance compensation: normalise passband gain to unity.
    sample *= (1.0f + res_ * 4.0f);

    // Algebraic soft-clip feedback: x/(1+|x|) — gentler than tanh, cleaner CEM character
    float feedback = stage_[3] * res_ * 4.0f;
    float soft_fb  = feedback / (1.0f + std::abs(feedback));
    float input    = sample - soft_fb;

    // Linear stage updates (no per-stage saturation — CEM/IR3109 characteristic)
    stage_[0] += g_ * (input     - stage_[0]);
    stage_[1] += g_ * (stage_[0] - stage_[1]);
    stage_[2] += g_ * (stage_[1] - stage_[2]);
    stage_[3] += g_ * (stage_[2] - stage_[3]);

    sample = stage_[3];
}

namespace {
[[maybe_unused]] const bool kRegistered = ModuleRegistry::instance().register_module(
    "SH_FILTER",
    "SH-style / CEM / IR3109 4-pole ladder LP (24 dB/oct) — clean, liquid, resonant",
    "The SH-101 / Juno filter character: liquid and transparent at low resonance, "
    "singing and focused at high resonance. kybd_cv is auto-injected on note_on "
    "to shift cutoff in proportion to MIDI note (1V/oct keyboard tracking). "
    "Use for resonant-noise percussion (wood blocks, tuned drums) at resonance > 0.7.",
    [](int sr) { return std::make_unique<CemFilterProcessor>(sr); }
);
} // namespace

} // namespace audio
