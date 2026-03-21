#include "AudioInputProcessor.hpp"
#include "../../core/ModuleRegistry.hpp"

namespace {
[[maybe_unused]] const bool kRegistered = audio::ModuleRegistry::instance().register_module(
    "AUDIO_INPUT",
    "Hardware line/microphone input source — outputs silence until HAL integration (Phase 25+)",
    "Use as the first chain node to represent a hardware audio input. "
    "Call engine_open_audio_input(handle, device_index) to connect to a hardware device. "
    "Currently outputs silence; full HAL integration is deferred to Phase 25. "
    "Role: SOURCE.",
    [](int sr) { return std::make_unique<audio::AudioInputProcessor>(sr); }
);
} // namespace
