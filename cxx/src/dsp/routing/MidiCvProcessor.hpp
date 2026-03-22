/**
 * @file MidiCvProcessor.hpp
 * @brief MIDI-to-CV source module — exposes keyboard state as patchable CV signals.
 *
 * Type name: MIDI_CV
 *
 * Role: SOURCE (PORT_CONTROL outputs only).
 *
 * Ports (PORT_CONTROL, all OUT):
 *   pitch_cv      1 V/oct; C4 (MIDI 60) = 0 V; +1 V per octave
 *   gate_cv       0 V (no key held) / 1 V (key held)
 *   velocity_cv   Note-on velocity, normalised 0–1 (holds until next note-on)
 *   aftertouch_cv Channel aftertouch, normalised 0–1 (default 0)
 *
 * The module is driven by Voice via on_note_on() / on_note_off() lifecycle
 * callbacks, exactly like ADSR_ENVELOPE.  engine_note_on/off → Voice::on_note_on/off
 * → MidiCvProcessor::on_note_on/off, which update internal state read by do_pull.
 *
 * pitch_cv convention:
 *   cv = (midi_note - 60) / 12.0f
 *   C4 = 0.0, C5 = 1.0, C3 = -1.0, A4 = 0.75
 *
 * Intended wiring (canonical Phase 27E patch):
 *   KBD.pitch_cv  → VCO.pitch_base_cv    (absolute pitch)
 *   KBD.gate_cv   → ENV.gate_in          (envelope trigger)
 *   KBD.velocity_cv → VCA.initial_gain_cv (velocity sensitivity, optional)
 */

#ifndef MIDI_CV_PROCESSOR_HPP
#define MIDI_CV_PROCESSOR_HPP

#include "../Processor.hpp"
#include <span>
#include <string>

namespace audio {

class MidiCvProcessor : public Processor {
public:
    explicit MidiCvProcessor(int /*sample_rate*/ = 48000) {
        declare_port({"pitch_cv",      PORT_CONTROL, PortDirection::OUT, false,
                      "1 V/oct keyboard pitch CV. C4 (MIDI 60) = 0 V; +1 V per octave up."});
        declare_port({"gate_cv",       PORT_CONTROL, PortDirection::OUT, false,
                      "Gate high (1.0) while a key is held, low (0.0) otherwise."});
        declare_port({"velocity_cv",   PORT_CONTROL, PortDirection::OUT, false,
                      "Note-on velocity normalised to [0, 1]. Holds until next note-on."});
        declare_port({"aftertouch_cv", PORT_CONTROL, PortDirection::OUT, false,
                      "Channel aftertouch normalised to [0, 1]. Default 0."});
    }

    // MIDI_CV has no audio output — it is a pure PORT_CONTROL source.
    // output_port_type returns PORT_CONTROL so Voice places it in mod_sources_.
    PortType output_port_type() const override { return PortType::PORT_CONTROL; }

    void reset() override {
        pitch_cv_      = 0.0f;
        gate_cv_       = 0.0f;
        velocity_cv_   = 0.0f;
        aftertouch_cv_ = 0.0f;
    }

    // Driven by Voice::on_note_on — sets pitch/gate/velocity for the next block.
    void on_note_on(double frequency) override {
        // Convert frequency back to V/oct relative to C4 (261.63 Hz).
        const double octaves = std::log2(frequency / 261.63);
        pitch_cv_    = static_cast<float>(octaves);
        gate_cv_     = 1.0f;
        velocity_cv_ = pending_velocity_;
    }

    void on_note_off() override {
        gate_cv_ = 0.0f;
    }

    // Called by Voice before on_note_on to store the incoming velocity.
    void on_note_velocity(float v) override { pending_velocity_ = v; }
    void set_pending_velocity(float v) { pending_velocity_ = v; }

    // Processor virtuals: declare secondary CV output ports so Voice::pull_mono
    // can allocate a dedicated block-wide buffer for each one.
    bool provides_named_cv(std::string_view port) const override {
        return port == "gate_cv" || port == "velocity_cv" || port == "aftertouch_cv";
    }
    float get_cv_output(std::string_view port) const override {
        return get_named_cv(port);
    }

    // Named port output selection — called by Voice's find_ctrl infrastructure.
    // do_pull fills the primary output buffer with pitch_cv (the most common
    // destination).  gate_cv / velocity_cv / aftertouch_cv are exposed via
    // get_named_cv() which Voice::pull_mono reads for non-primary ports.
    float get_named_cv(std::string_view port) const {
        if (port == "pitch_cv")      return pitch_cv_;
        if (port == "gate_cv")       return gate_cv_;
        if (port == "velocity_cv")   return velocity_cv_;
        if (port == "aftertouch_cv") return aftertouch_cv_;
        return 0.0f;
    }

protected:
    // do_pull: fills the output buffer with pitch_cv (primary output).
    // Gate, velocity, and aftertouch are delivered via get_named_cv().
    void do_pull(std::span<float> output, const VoiceContext* = nullptr) override {
        std::fill(output.begin(), output.end(), pitch_cv_);
    }

private:
    float pitch_cv_        = 0.0f;
    float gate_cv_         = 0.0f;
    float velocity_cv_     = 0.0f;
    float aftertouch_cv_   = 0.0f;
    float pending_velocity_= 0.0f;
};

} // namespace audio

#endif // MIDI_CV_PROCESSOR_HPP
