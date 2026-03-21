#pragma once
#include "../Processor.hpp"

namespace audio {

/**
 * @brief AUDIO_INPUT — hardware line/microphone input source (Phase 27C).
 *
 * Placeholder: outputs silence until engine_open_audio_input() connects it
 * to a live hardware input stream. The HAL implementation is deferred.
 *
 * Role: SOURCE (audio_out only).
 * Parameters: device_index (int, default 0), gain (float, 0–4, default 1).
 */
class AudioInputProcessor : public Processor {
public:
    explicit AudioInputProcessor(int /*sample_rate*/ = 48000) {
        declare_port({"audio_out", PORT_AUDIO, PortDirection::OUT,
                      false, "Hardware input audio output"});
        declare_parameter({"device_index", "Device Index", 0.0f, 16.0f, 0.0f,
                           false, "Hardware input device index (0 = default)"});
        declare_parameter({"gain", "Input Gain", 0.0f, 4.0f, 1.0f,
                           false, "Input gain multiplier"});
    }

    void reset() override {}
    bool reset_on_note_on() const override { return false; }

    bool apply_parameter(const std::string& name, float value) override {
        if (name == "device_index") { device_index_ = static_cast<int>(value); return true; }
        if (name == "gain")         { gain_ = value; return true; }
        return false;
    }

    int device_index() const { return device_index_; }

protected:
    // Outputs silence — live input requires HAL integration (deferred, Phase 25+).
    void do_pull(std::span<float> output, const VoiceContext* /*ctx*/) override {
        std::fill(output.begin(), output.end(), 0.0f);
    }

private:
    int   device_index_ = 0;
    float gain_         = 1.0f;
};

} // namespace audio
