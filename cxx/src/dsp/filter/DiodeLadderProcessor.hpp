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
        , cutoff_(20000.0f) // DEFAULT SAFETY: Fully open
        , res_(0.0f)
    {
        for (int i = 0; i < 4; ++i) stage_[i] = 0.0f;
        update_coefficients();

        // Phase 15: named port declarations
        declare_port({"audio_in",     PORT_AUDIO,   PortDirection::IN});
        declare_port({"audio_out",    PORT_AUDIO,   PortDirection::OUT});
        declare_port({"cutoff_cv",    PORT_CONTROL, PortDirection::IN,  false}); // bipolar [-1,1]
        declare_port({"resonance_cv", PORT_CONTROL, PortDirection::IN,  true});  // unipolar [0,1]

        declare_parameter({"cutoff",    "Cutoff Frequency", 20.0f, 20000.0f, 20000.0f, true});
        declare_parameter({"resonance", "Resonance",         0.0f,     1.0f,     0.0f});
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
            float combined = (output.left[i] + output.right[i]) * 0.5f;
            process_sample(combined);
            output.left[i] = combined;
            output.right[i] = combined;
        }
    }

private:
    inline void process_sample(float& sample) {
        float input = sample;
        
        // TB-303 Feedback: The 17.0f multiplier is too high for this topology
        // Reducing to a more stable value and using tanh to clamp feedback
        float feedback = std::tanh(stage_[3] * res_ * 4.0f);
        input -= feedback;

        float s0 = stage_[0], s1 = stage_[1], s2 = stage_[2], s3 = stage_[3];
        
        // Non-linear stage updates
        stage_[0] += g_ * (std::tanh(input) - std::tanh(s0));
        stage_[1] += g_ * (std::tanh(stage_[0]) - std::tanh(s1));
        stage_[2] += g_ * (std::tanh(stage_[1]) - std::tanh(s2));
        stage_[3] += g_ * (std::tanh(stage_[2]) - std::tanh(s3));

        sample = stage_[3];

        if (std::isnan(sample) || std::isinf(sample)) {
            reset();
            sample = 0.0f;
        }
    }

    void update_coefficients() {
        // Correcting the g coefficient for the TB-303 style coupled poles
        float f = static_cast<float>(cutoff_ / sample_rate_);
        g_ = std::tan(static_cast<float>(M_PI) * f); 
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
