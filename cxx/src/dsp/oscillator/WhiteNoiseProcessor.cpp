/**
 * @file WhiteNoiseProcessor.cpp
 * @brief Pink noise filter, do_pull, and self-registration.
 */
#include "WhiteNoiseProcessor.hpp"
#include "../../core/ModuleRegistry.hpp"
#include <cmath>

namespace audio {

WhiteNoiseProcessor::WhiteNoiseProcessor() : state_(0xDEADBEEFu) {
    declare_port({"audio_out", PORT_AUDIO, PortDirection::OUT});
    declare_parameter({"color", "Noise Color", 0.0f, 1.0f, 0.0f});
}

void WhiteNoiseProcessor::reset() {
    state_ = 0xDEADBEEFu;
    b0_ = b1_ = b2_ = b3_ = b4_ = b5_ = b6_ = 0.0f;
}

bool WhiteNoiseProcessor::apply_parameter(const std::string& name, float value) {
    if (name == "color") {
        color_ = static_cast<int>(std::round(value));
        return true;
    }
    return false;
}

float WhiteNoiseProcessor::tick_pink() {
    const float white = tick();
    b0_ = 0.99886f * b0_ + white * 0.0555179f;
    b1_ = 0.99332f * b1_ + white * 0.0750759f;
    b2_ = 0.96900f * b2_ + white * 0.1538520f;
    b3_ = 0.86650f * b3_ + white * 0.3104856f;
    b4_ = 0.55000f * b4_ + white * 0.5329522f;
    b5_ = -0.7616f * b5_ - white * 0.0168980f;
    const float pink = (b0_ + b1_ + b2_ + b3_ + b4_ + b5_ + b6_ + white * 0.5362f) * 0.11f;
    b6_ = white * 0.115926f;
    return pink;
}

void WhiteNoiseProcessor::do_pull(std::span<float> output, const VoiceContext* /*ctx*/) {
    if (color_ == 1) {
        for (auto& s : output) s = tick_pink();
    } else {
        for (auto& s : output) s = tick();
    }
}

namespace {
[[maybe_unused]] const bool kRegistered = ModuleRegistry::instance().register_module(
    "WHITE_NOISE",
    "LCG-based white noise generator (PORT_AUDIO, range [-1, 1])",
    "Route through a resonant filter (SH_FILTER res > 0.7) to create tuned "
    "percussion (wood blocks, snares). Mix with a tonal VCO via AUDIO_MIXER "
    "for body+attack layering. No parameters — always outputs full-amplitude noise.",
    [](int /*sr*/) { return std::make_unique<WhiteNoiseProcessor>(); }
);
} // namespace

} // namespace audio
