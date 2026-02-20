/**
 * @file DiodeLadderProcessor.hpp
 * @brief TB-303 style diode ladder filter.
 */

#ifndef AUDIO_DIODE_LADDER_PROCESSOR_HPP
#define AUDIO_DIODE_LADDER_PROCESSOR_HPP

#include "FilterProcessor.hpp"
#include <cmath>
#include <algorithm>

namespace audio {

/**
 * @brief Diode-ladder filter (TB-303 style).
 * 
 * Characteristic 'squelchy' sound with coupled poles.
 */
class DiodeLadderProcessor : public FilterProcessor {
public:
    explicit DiodeLadderProcessor(int sample_rate)
        : sample_rate_(sample_rate)
        , cutoff_(1000.0f)
        , res_(0.0f)
    {
        for (int i = 0; i < 4; ++i) stage_[i] = 0.0f;
        update_coefficients();
    }

    void set_cutoff(float frequency) override {
        cutoff_ = std::clamp(frequency, 20.0f, sample_rate_ * 0.45f);
        update_coefficients();
    }

    void set_resonance(float resonance) override {
        res_ = std::clamp(resonance, 0.0f, 1.0f);
        update_coefficients();
    }

    void reset() override {
        for (int i = 0; i < 4; ++i) stage_[i] = 0.0f;
    }

protected:
    void do_pull(std::span<float> output, const VoiceContext* /* voice_context */ = nullptr) override {
        for (auto& sample : output) {
            // DIODE LADDER ALGORITHM (Approximation)
            // Diode ladders have coupled stages and a specific feedback gain.
            
            float input = sample;
            
            // Resonance feedback with TB-303 style bass-loss
            // The feedback is taken from the 4th stage and fed back to the 1st.
            float feedback = stage_[3] * res_ * 17.0f; // High gain for resonance
            input -= std::tanh(feedback);

            // Coupled 1-pole stages
            // In a diode ladder, each stage's state depends on the others.
            // This is a simplified ZDF-like approximation for the coupled stages.
            float s0 = stage_[0], s1 = stage_[1], s2 = stage_[2], s3 = stage_[3];
            
            stage_[0] += g_ * (input - s0 - 0.5f * (s1));
            stage_[1] += g_ * (stage_[0] - s1 - 0.5f * (s2));
            stage_[2] += g_ * (stage_[1] - s2 - 0.5f * (s3));
            stage_[3] += g_ * (stage_[2] - s3);

            sample = stage_[3];
        }
    }

private:
    void update_coefficients() {
        g_ = static_cast<float>(2.0 * M_PI * cutoff_ / sample_rate_);
        g_ = std::clamp(g_, 0.0f, 1.0f);
    }

    int sample_rate_;
    float cutoff_;
    float res_;
    float g_;
    float stage_[4];
};

} // namespace audio

#endif // AUDIO_DIODE_LADDER_PROCESSOR_HPP
