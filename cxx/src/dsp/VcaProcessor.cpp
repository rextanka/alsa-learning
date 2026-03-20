/**
 * @file VcaProcessor.cpp
 * @brief VCA self-registration.
 */
#include "VcaProcessor.hpp"
#include "../core/ModuleRegistry.hpp"

namespace audio {

namespace {
[[maybe_unused]] const bool kRegistered = ModuleRegistry::instance().register_module(
    "VCA",
    "Voltage Controlled Amplifier — multiplies audio by a gain CV envelope",
    "Place at the end of the signal chain. Connect ADSR:envelope_out → VCA:gain_cv. "
    "initial_gain sets the resting amplitude when no CV is connected. "
    "response_curve=1.0 applies exponential (dB-linear) gain law for natural dynamics.",
    [](int sr) { return std::make_unique<VcaProcessor>(sr); }
);
} // namespace

} // namespace audio
