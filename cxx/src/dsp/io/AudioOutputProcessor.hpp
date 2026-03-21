#pragma once
#include "../Processor.hpp"

namespace audio {

/**
 * @brief AUDIO_OUTPUT — explicit chain terminator (Phase 27C).
 *
 * A transparent SINK: receives audio via the inline audio path and passes
 * it through unchanged. Gives patch editors a named "audio output" endpoint.
 *
 * Role: SINK (audio_in only — no audio_out).
 * Position in chain: last node only (bake() SINK exception).
 * do_pull() is a no-op — inline audio from the predecessor passes through.
 */
class AudioOutputProcessor : public Processor {
public:
    explicit AudioOutputProcessor(int /*sample_rate*/ = 48000) {
        declare_port({"audio_in", PORT_AUDIO, PortDirection::IN,
                      false, "Audio input from the last chain processor"});
    }
    void reset() override {}
    bool reset_on_note_on() const override { return false; }

protected:
    void do_pull(std::span<float> /*output*/, const VoiceContext* /*ctx*/) override {}
};

} // namespace audio
