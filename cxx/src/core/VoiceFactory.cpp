/**
 * @file VoiceFactory.cpp
 * @brief VoiceFactory implementation (Phase 14).
 */

#include "VoiceFactory.hpp"
#include "routing/CompositeGenerator.hpp"
#include "envelope/AdsrEnvelopeProcessor.hpp"
#include "VcaProcessor.hpp"

namespace audio {

std::unique_ptr<Voice> VoiceFactory::createSH101(int sample_rate) {
    auto voice = std::make_unique<Voice>(sample_rate);

    // --- [0] CompositeGenerator — VCO + SourceMixer ---
    auto gen = std::make_unique<CompositeGenerator>(sample_rate);

    // Match existing Voice constructor defaults:
    //   source_mixer_->set_gain(1, 1.0f)  — pulse (ch 1)
    //   source_mixer_->set_gain(2, 0.5f)  — sub   (ch 2)
    gen->mixer().set_gain(1, 1.0f);
    gen->mixer().set_gain(2, 0.5f);

    voice->add_processor(std::move(gen), "VCO");

    // --- [1] AdsrEnvelopeProcessor — PORT_CONTROL output ---
    auto env = std::make_unique<AdsrEnvelopeProcessor>(sample_rate);
    env->set_attack_time(0.05f);
    env->set_decay_time(0.10f);
    env->set_sustain_level(0.7f);
    env->set_release_time(0.10f);
    voice->add_processor(std::move(env), "ENV");

    // --- [2] VcaProcessor — multiplies audio by envelope level ---
    voice->add_processor(std::make_unique<VcaProcessor>(), "VCA");

    // Validate and mark ready.
    voice->bake();

    return voice;
}

} // namespace audio
