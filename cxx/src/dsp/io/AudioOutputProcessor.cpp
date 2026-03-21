#include "AudioOutputProcessor.hpp"
#include "../../core/ModuleRegistry.hpp"

namespace {
[[maybe_unused]] const bool kRegistered = audio::ModuleRegistry::instance().register_module(
    "AUDIO_OUTPUT",
    "Explicit audio chain terminator — marks the final output node for patch editors",
    "Add as the last node in a chain when your patch editor requires an explicit output "
    "endpoint. The engine routes audio identically whether or not AUDIO_OUTPUT is present. "
    "Role: SINK. Only valid as the last node in the chain.",
    [](int sr) { return std::make_unique<audio::AudioOutputProcessor>(sr); }
);
} // namespace
