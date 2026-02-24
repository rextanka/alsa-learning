/**
 * @file MonoToStereoProcessor.hpp
 * @brief Utility for interleaving mono signals into stereo buffers.
 * 
 * Part of Phase 13/14 preparation for Mixer and FX.
 */

#ifndef AUDIO_MONO_TO_STEREO_PROCESSOR_HPP
#define AUDIO_MONO_TO_STEREO_PROCESSOR_HPP

#include <span>
#include <cassert>
#include <cstddef>

namespace audio {

/**
 * @brief A non-owning functional filter for mono-to-stereo conversion.
 * 
 * Bridges the gap between mono DSP chains and stereo hardware outputs.
 * Follows the "Mono-until-Stereo" philosophy from ARCH_PLAN.md.
 */
class MonoToStereoProcessor {
public:
    /**
     * @brief Interleaves mono input into stereo output (L=R).
     * 
     * @param mono_input Input span of mono samples.
     * @param interleaved_stereo_output Output span of interleaved stereo samples (2x size of input).
     */
    static void process(std::span<const float> mono_input, std::span<float> interleaved_stereo_output) {
        // Interleaved stereo requires 2x the samples
        assert(interleaved_stereo_output.size() >= mono_input.size() * 2);

        for (size_t i = 0; i < mono_input.size(); ++i) {
            float sample = mono_input[i];
            interleaved_stereo_output[i * 2] = sample;     // Left
            interleaved_stereo_output[i * 2 + 1] = sample; // Right
        }
    }
};

} // namespace audio

#endif // AUDIO_MONO_TO_STEREO_PROCESSOR_HPP
