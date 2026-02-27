/**
 * @file MoogLadderProcessor.hpp
 * @brief 4-pole transistor ladder filter (Moog style).
 */

#ifndef AUDIO_MOOG_LADDER_PROCESSOR_HPP
#define AUDIO_MOOG_LADDER_PROCESSOR_HPP

#include "FilterProcessor.hpp"
#include <cmath>
#include <algorithm>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace audio {

/**
 * @brief Moog-style 4-pole ladder filter.
 * 
 * Uses 4 stages of 1-pole filters with feedback.
 */
class MoogLadderProcessor : public FilterProcessor {
public:
    explicit MoogLadderProcessor(int sample_rate)
        : sample_rate_(sample_rate)
        , cutoff_(20000.0f) // DEFAULT SAFETY: Fully open
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
            process_sample(sample);
        }
    }

    void do_pull(AudioBuffer& output, const VoiceContext* /* voice_context */ = nullptr) override {
        for (size_t i = 0; i < output.frames(); ++i) {
            // Process L and R through the same filter state (dual-mono for now)
            // Note: For true stereo, we would need two sets of filter stages.
            // But let's follow the dual-mono instruction first.
            float combined = (output.left[i] + output.right[i]) * 0.5f;
            process_sample(combined);
            output.left[i] = combined;
            output.right[i] = combined;
        }
    }

private:
    inline void process_sample(float& sample) {
        float input = sample;
        float feedback = stage_[3] * res_ * 4.0f;
        input -= std::tanh(feedback);
        
        stage_[0] += g_ * (input - stage_[0]);
        stage_[1] += g_ * (stage_[0] - stage_[1]);
        stage_[2] += g_ * (stage_[1] - stage_[2]);
        stage_[3] += g_ * (stage_[2] - stage_[3]);
        
        sample = stage_[3];
    }

    void update_coefficients() {
        // Simple linear mapping for g (approximation)
        // w = 2 * PI * fc / fs
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

#endif // AUDIO_MOOG_LADDER_PROCESSOR_HPP
