/**
 * @file MoogLadderProcessor.cpp
 * @brief Moog ladder process_sample and self-registration.
 */
#include "MoogLadderProcessor.hpp"
#include "../../core/ModuleRegistry.hpp"
#include <cmath>

namespace audio {

void MoogLadderProcessor::process_sample(float& sample) {
    // Resonance compensation: normalise passband gain to unity.
    sample *= (1.0f + res_ * 4.0f);

    // Tanh-saturated feedback — the Moog-style signature
    float feedback = stage_[3] * res_ * 4.0f;
    float input    = sample - std::tanh(feedback);

    stage_[0] += g_ * (input     - stage_[0]);
    stage_[1] += g_ * (stage_[0] - stage_[1]);
    stage_[2] += g_ * (stage_[1] - stage_[2]);
    stage_[3] += g_ * (stage_[2] - stage_[3]);

    sample = stage_[3];
}

namespace {
[[maybe_unused]] const bool kRegistered = ModuleRegistry::instance().register_module(
    "MOOG_FILTER",
    "4-pole Moog transistor ladder LP (24 dB/oct) — smooth, creamy, thick",
    "The classic synthesizer lowpass. resonance > 0.8 approaches self-oscillation; "
    ">0.95 produces a pure sine at the cutoff frequency. "
    "Connect ENV → cutoff_cv for the characteristic filter sweep. "
    "fm_in enables audio-rate cutoff modulation for complex timbres.",
    [](int sr) { return std::make_unique<MoogLadderProcessor>(sr); }
);
} // namespace

} // namespace audio
