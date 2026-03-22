/**
 * @file MidiCvProcessor.cpp
 * @brief MIDI_CV source module — self-registration.
 */
#include "MidiCvProcessor.hpp"
#include "../../core/ModuleRegistry.hpp"

namespace audio {

namespace {
[[maybe_unused]] const bool kRegistered = ModuleRegistry::instance().register_module(
    "MIDI_CV",
    "MIDI-to-CV source — exposes keyboard pitch, gate, velocity, and aftertouch as patchable CV",
    "Default tag KBD. Wire KBD:pitch_cv → VCO:pitch_base_cv for absolute keyboard tracking. "
    "Wire KBD:gate_cv → ENV:gate_in to trigger envelopes from key presses. "
    "Wire KBD:velocity_cv → VCA:initial_gain_cv for velocity sensitivity (optional). "
    "pitch_cv convention: C4 (MIDI 60) = 0 V, +1 V per octave. "
    "gate_cv: 1.0 while key held, 0.0 released. "
    "M-132 gate-bias pattern: KBD:gate_cv → CV_MIXER:cv_in with negative offset "
    "so LFO re-triggers ADSR only while a key is held (Roland Fig 3-4 banjo).",
    [](int sr) { return std::make_unique<MidiCvProcessor>(sr); }
);
} // namespace

} // namespace audio
