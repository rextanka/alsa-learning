/**
 * @file SourceMixer.hpp
 * @brief 5-channel source mixer with soft-saturation (tanh) to emulate analog headroom.
 */

#ifndef SOURCE_MIXER_HPP
#define SOURCE_MIXER_HPP

#include "Processor.hpp"
#include <algorithm>
#include <cmath>
#include <array>

namespace audio {

/**
 * @brief 5-channel source mixer for synth oscillators and noise.
 * 
 * Channels:
 * 0: Sawtooth
 * 1: Pulse/Square
 * 2: Sub-Oscillator
 * 3: Noise
 * 4: External/Other
 * 
 * Features tanh-based soft-saturation to emulate analog growl when pushed.
 */
class SourceMixer : public Processor {
public:
    static constexpr size_t NUM_CHANNELS = 5;

    SourceMixer() {
        gains_.fill(0.0f);
    }

    /**
     * @brief Set gain for a specific channel.
     * @param channel Channel index (0-4)
     * @param gain Linear gain (0.0 to 1.0, can be pushed higher for saturation)
     */
    void set_gain(size_t channel, float gain) {
        if (channel < NUM_CHANNELS) {
            gains_[channel] = gain;
        }
    }

    /**
     * @brief Process a single sample set.
     * @param inputs Array of input samples
     * @return float Mixed and saturated output
     */
    float mix(const std::array<float, NUM_CHANNELS>& inputs) {
        float sum = 0.0f;
        for (size_t i = 0; i < NUM_CHANNELS; ++i) {
            sum += inputs[i] * gains_[i];
        }

        // Apply soft-saturation (tanh)
        // tanh(x) provides a smooth curve that levels off at +/- 1.0
        return std::tanh(sum);
    }

    void reset() override {
        // Gains are parameters, not state, so we don't reset them here.
    }

protected:
    // Mono pull is not directly used as mixer is typically embedded in Voice.
    void do_pull(std::span<float> output, const VoiceContext* /* context */ = nullptr) override {
        std::fill(output.begin(), output.end(), 0.0f);
    }

    void do_pull(AudioBuffer& output, const VoiceContext* /* context */ = nullptr) override {
        output.clear();
    }

private:
    std::array<float, NUM_CHANNELS> gains_;
};

} // namespace audio

#endif // SOURCE_MIXER_HPP
