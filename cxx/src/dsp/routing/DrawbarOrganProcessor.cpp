/**
 * @file DrawbarOrganProcessor.cpp
 * @brief Drawbar organ do_pull and self-registration.
 */
#include "DrawbarOrganProcessor.hpp"
#include "../../core/ModuleRegistry.hpp"

namespace audio {

DrawbarOrganProcessor::DrawbarOrganProcessor(int sample_rate)
    : sample_rate_(sample_rate)
    , ramp_samples_(static_cast<int>(static_cast<float>(sample_rate) * kRampSeconds))
    , base_freq_(0.0)
{
    for (size_t i = 0; i < NUM_DRAWBARS; ++i)
        oscs_[i] = std::make_unique<SineOscillatorProcessor>(sample_rate);

    // Default: 8' drawbar fully open (classic "flute" preset)
    drawbar_gains_[2].set_target(8.0f, 0); // snap to initial value

    set_tag("ORGAN");

    declare_port({"audio_out", PORT_AUDIO,   PortDirection::OUT});
    declare_port({"pitch_cv",  PORT_CONTROL, PortDirection::IN, false});

    declare_parameter({"drawbar_16",  "16' Sub-Octave",  0.0f, 8.0f, 0.0f});
    declare_parameter({"drawbar_513", "5⅓' Quint",      0.0f, 8.0f, 0.0f});
    declare_parameter({"drawbar_8",   "8' Principal",    0.0f, 8.0f, 8.0f});
    declare_parameter({"drawbar_4",   "4' Octave",       0.0f, 8.0f, 0.0f});
    declare_parameter({"drawbar_223", "2⅔' Nazard",     0.0f, 8.0f, 0.0f});
    declare_parameter({"drawbar_2",   "2' Super-Octave", 0.0f, 8.0f, 0.0f});
    declare_parameter({"drawbar_135", "1⅗' Tierce",     0.0f, 8.0f, 0.0f});
    declare_parameter({"drawbar_113", "1⅓' Larigot",    0.0f, 8.0f, 0.0f});
    declare_parameter({"drawbar_1",   "1' Sifflöte",    0.0f, 8.0f, 0.0f});
}

bool DrawbarOrganProcessor::apply_parameter(const std::string& name, float value) {
    static constexpr const char* kParamNames[NUM_DRAWBARS] = {
        "drawbar_16", "drawbar_513", "drawbar_8",   "drawbar_4",
        "drawbar_223", "drawbar_2",  "drawbar_135", "drawbar_113",
        "drawbar_1"
    };
    for (size_t i = 0; i < NUM_DRAWBARS; ++i) {
        if (name == kParamNames[i]) {
            drawbar_gains_[i].set_target(value, ramp_samples_);
            return true;
        }
    }
    return false;
}

void DrawbarOrganProcessor::do_pull(std::span<float> output, const VoiceContext*) {
    constexpr float kNorm = 1.0f / 9.0f;

    const int n_frames = static_cast<int>(output.size());
    for (size_t i = 0; i < NUM_DRAWBARS; ++i)
        drawbar_gains_[i].advance(n_frames);

    for (size_t n = 0; n < output.size(); ++n) {
        float sum = 0.0f;
        for (size_t i = 0; i < NUM_DRAWBARS; ++i) {
            const float gain = drawbar_gains_[i].get();
            if (gain > 0.0f)
                sum += static_cast<float>(oscs_[i]->tick()) * (gain / 8.0f);
        }
        output[n] = sum * kNorm;
    }
}

namespace {
[[maybe_unused]] const bool kRegistered = ModuleRegistry::instance().register_module(
    "DRAWBAR_ORGAN",
    "Tonewheel organ — 9 sine partials at Hammond footage ratios (16'..1')",
    "Set drawbar_8=8 for classic 8' principal. Add drawbar_4=4 for octave brightness. "
    "drawbar_16=4 for sub-octave warmth. drawbar_223 + drawbar_135 for gospel growl. "
    "Connect ADSR with slow attack/release for soft swell pedal effect.",
    [](int sr) { return std::make_unique<DrawbarOrganProcessor>(sr); }
);
} // namespace

} // namespace audio
