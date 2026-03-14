#pragma once
#include <vector>
#include <span>
#include <cmath>
#include <algorithm>
#include "AudioBuffer.hpp"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace audio {

/**
 * @brief Global Summing Bus for polyphonic voice aggregation.
 * 
 * Standardizes signal routing by replacing intermediate Mixer stages.
 * Implements Constant Power Panning and provides master headroom attenuation.
 */
class SummingBus {
public:
    /**
     * @brief Master attenuation to provide headroom for polyphony.
     * 0.15f matches the previous legacy master gain in VoiceManager.
     */
    static constexpr float MASTER_GAIN = 0.15f;

    explicit SummingBus(size_t block_size = 512) {
        left_buffer_.resize(block_size, 0.0f);
        right_buffer_.resize(block_size, 0.0f);
    }

    /**
     * @brief Clear the bus at the start of each processing cycle.
     */
    void clear() {
        std::fill(left_buffer_.begin(), left_buffer_.end(), 0.0f);
        std::fill(right_buffer_.begin(), right_buffer_.end(), 0.0f);
    }

    /**
     * @brief Add a mono voice contribution to the stereo bus.
     * 
     * Implements Constant Power Panning: L^2 + R^2 = 1.
     * 
     * @param mono Input mono signal.
     * @param pan Panning position (-1.0 for Left, 1.0 for Right, 0.0 for Center).
     */
    void add_voice(const std::span<float> mono, float pan) {
        if (left_buffer_.size() < mono.size()) {
            left_buffer_.resize(mono.size(), 0.0f);
            right_buffer_.resize(mono.size(), 0.0f);
        }

        // Map pan [-1, 1] to theta [0, PI/2]
        // pan -1 (Left) -> 0
        // pan  0 (Center) -> PI/4
        // pan  1 (Right) -> PI/2
        float theta = (pan + 1.0f) * (static_cast<float>(M_PI) / 4.0f);
        float gain_l = std::cos(theta) * MASTER_GAIN;
        float gain_r = std::sin(theta) * MASTER_GAIN;

        for (size_t i = 0; i < mono.size(); ++i) {
            left_buffer_[i] += mono[i] * gain_l;
            right_buffer_[i] += mono[i] * gain_r;
        }
    }

    /**
     * @brief Output the summed result into a stereo AudioBuffer.
     * 
     * Applies final soft-clipping to prevent digital distortion.
     */
    void pull(AudioBuffer& output) {
        const size_t frames = std::min(output.frames(), left_buffer_.size());
        
        auto soft_clip = [](float s) {
            if (s > 0.95f) return 0.95f + 0.05f * std::tanh((s - 0.95f) / 0.05f);
            if (s < -0.95f) return -0.95f + 0.05f * std::tanh((s + 0.95f) / 0.05f);
            return s;
        };

        for (size_t i = 0; i < frames; ++i) {
            output.left[i] = soft_clip(left_buffer_[i]);
            output.right[i] = soft_clip(right_buffer_[i]);
        }
    }

    /**
     * @brief Direct access to internal buffers (for effects processing inserts).
     */
    const std::vector<float>& left() const { return left_buffer_; }
    const std::vector<float>& right() const { return right_buffer_; }

private:
    std::vector<float> left_buffer_;
    std::vector<float> right_buffer_;
};

} // namespace audio
