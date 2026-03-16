/**
 * @file VoiceFactory.hpp
 * @brief Factory for constructing pre-configured Voice signal chains (Phase 14).
 *
 * Each static method builds a Voice, populates its signal_chain_ via
 * add_processor(), and calls bake() before returning. The returned Voice is
 * ready to be played — no further chain configuration is required.
 *
 * Topologies:
 *   createSH101 — SourceMixer (pulse+saw+sub+sine+tri+wavetable) → VCF (optional)
 *                 → ENV (ADSR) → VCA
 */

#ifndef VOICE_FACTORY_HPP
#define VOICE_FACTORY_HPP

#include "Voice.hpp"
#include <memory>

namespace audio {

class VoiceFactory {
public:
    VoiceFactory() = delete;

    /**
     * @brief Construct an SH-101 style voice.
     *
     * Signal chain:
     *   [0] CompositeGenerator  tag="VCO"  — all oscillators + SourceMixer
     *   [1] AdsrEnvelopeProcessor tag="ENV" — PORT_CONTROL output
     *   [2] VcaProcessor          tag="VCA" — multiplies audio by ENV level
     *
     * No filter is added by default (matching the existing Voice constructor
     * default of filter_=nullptr). Call voice->set_filter_type() after creation
     * to insert a filter; the chain will be updated accordingly.
     *
     * Default mixer gains:
     *   pulse_gain = 1.0 (ch 1), sub_gain = 0.5 (ch 2) — matches existing ctor.
     *
     * Default ADSR: attack=50ms, decay=100ms, sustain=0.7, release=100ms.
     */
    static std::unique_ptr<Voice> createSH101(int sample_rate);
};

} // namespace audio

#endif // VOICE_FACTORY_HPP
