/**
 * @file SummingMixer.hpp
 * @brief Polyphonic summing mixer for accumulating multiple audio sources.
 */

#ifndef AUDIO_SUMMING_MIXER_HPP
#define AUDIO_SUMMING_MIXER_HPP

#include "../Processor.hpp"
#include <vector>
#include <memory>
#include <algorithm>
#include <span>

namespace audio {

/**
 * @brief Accumulates multiple input processors into a single stereo output.
 * 
 * This class fulfills Task 2 of the Senior Audio Architect's objective.
 * It is RT-safe and performs master safety clamping.
 */
class SummingMixer : public Processor {
public:
    SummingMixer() = default;

    /**
     * @brief Add a source to the mixer.
     * 
     * Note: Not RT-safe. Call before starting the audio driver.
     */
    void add_source(std::shared_ptr<Processor> source) {
        if (source) {
            sources_.push_back(source);
        }
    }

    /**
     * @brief Reset all internal sources.
     */
    void reset() override {
        for (auto& source : sources_) {
            source->reset();
        }
    }

protected:
    /**
     * @brief Pull and sum all sources. (Mono version)
     */
    void do_pull(std::span<float> output, const VoiceContext* voice_context = nullptr) override {
        std::fill(output.begin(), output.end(), 0.0f);
        
        for (auto& source : sources_) {
            float scratch[1024]; 
            const size_t frames = std::min(output.size(), static_cast<size_t>(1024));
            std::span<float> scratch_span(scratch, frames);
            
            source->pull(scratch_span, voice_context);
            
            for (size_t i = 0; i < frames; ++i) {
                output[i] += scratch[i];
            }
        }

        // Master Safety Clamp
        for (float& sample : output) {
            sample = std::clamp(sample, -1.0f, 1.0f);
        }
    }

    /**
     * @brief Pull and sum all sources. (Stereo version)
     */
    void do_pull(AudioBuffer& output, const VoiceContext* voice_context = nullptr) override {
        output.clear();
        
        const size_t block_size = output.left.size();
        float scratch_l[1024];
        float scratch_r[1024];
        const size_t frames = std::min(block_size, static_cast<size_t>(1024));

        for (auto& source : sources_) {
            AudioBuffer scratch_buffer;
            scratch_buffer.left = std::span<float>(scratch_l, frames);
            scratch_buffer.right = std::span<float>(scratch_r, frames);
            scratch_buffer.clear();

            source->pull(scratch_buffer, voice_context);

            for (size_t i = 0; i < frames; ++i) {
                output.left[i] += scratch_l[i];
                output.right[i] += scratch_r[i];
            }
        }

        // Master Safety Clamp
        for (size_t i = 0; i < frames; ++i) {
            output.left[i] = std::clamp(output.left[i], -1.0f, 1.0f);
            output.right[i] = std::clamp(output.right[i], -1.0f, 1.0f);
        }
    }

private:
    std::vector<std::shared_ptr<Processor>> sources_;
};

} // namespace audio

#endif // AUDIO_SUMMING_MIXER_HPP
