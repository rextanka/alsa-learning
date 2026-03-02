/**
 * @file AudioBuffer.hpp
 * @brief Structure representing a multi-channel audio buffer.
 */

#ifndef AUDIO_AUDIO_BUFFER_HPP
#define AUDIO_AUDIO_BUFFER_HPP

#include <span>
#include <vector>

namespace audio {

/**
 * @brief Represents a stereo (2-channel) audio buffer.
 * 
 * Uses separate spans for left and right channels to support
 * both interleaved and non-interleaved processing.
 */
struct AudioBuffer {
    std::span<float> left;
    std::span<float> right;

    /**
     * @brief Underlying storage if this buffer owns its memory.
     */
    std::vector<float> left_storage;
    std::vector<float> right_storage;

    size_t frames() const { return left.size(); }

    /**
     * @brief Resize the buffer (if it owns storage).
     */
    void resize(size_t frames, size_t channels = 2) {
        left_storage.resize(frames, 0.0f);
        left = std::span<float>(left_storage);
        
        if (channels > 1) {
            right_storage.resize(frames, 0.0f);
            right = std::span<float>(right_storage);
        } else {
            right_storage.clear();
            right = std::span<float>();
        }
    }

    /**
     * @brief Zero out the buffer.
     */
    void clear() {
        if (!left.empty()) std::fill(left.begin(), left.end(), 0.0f);
        if (!right.empty()) std::fill(right.begin(), right.end(), 0.0f);
    }
};

} // namespace audio

#endif // AUDIO_AUDIO_BUFFER_HPP
