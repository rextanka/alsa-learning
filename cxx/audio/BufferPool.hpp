/**
 * @file BufferPool.hpp
 * @brief Manages a pool of pre-allocated audio buffers to avoid heap allocation.
 */

#ifndef AUDIO_BUFFER_POOL_HPP
#define AUDIO_BUFFER_POOL_HPP

#include <vector>
#include <memory>
#include <mutex>
#include <span>
#include <functional>

namespace audio {

/**
 * @brief Represents a stereo memory block (L/R vectors).
 */
struct StereoBlock {
    std::vector<float> left;
    std::vector<float> right;
    
    explicit StereoBlock(size_t frames) 
        : left(frames, 0.0f)
        , right(frames, 0.0f)
    {}
};

/**
 * @brief Pool of audio buffers for efficient processing.
 */
class BufferPool {
public:
    /**
     * @param buffer_size Size of each buffer in frames.
     * @param initial_capacity Number of buffers to pre-allocate.
     */
    explicit BufferPool(size_t buffer_size, size_t initial_capacity = 32)
        : buffer_size_(buffer_size)
    {
        for (size_t i = 0; i < initial_capacity; ++i) {
            pool_.push_back(std::make_unique<StereoBlock>(buffer_size_));
        }
    }

    /**
     * @brief Borrow a stereo block from the pool.
     */
    using BufferPtr = std::unique_ptr<StereoBlock, std::function<void(StereoBlock*)>>;

    BufferPtr borrow() {
        std::lock_guard<std::mutex> lock(mutex_);
        
        std::unique_ptr<StereoBlock> buffer;
        if (pool_.empty()) {
            buffer = std::make_unique<StereoBlock>(buffer_size_);
        } else {
            buffer = std::move(pool_.back());
            pool_.pop_back();
        }

        // Custom deleter returns the buffer to the pool
        auto deleter = [this](StereoBlock* b) {
            std::lock_guard<std::mutex> lock(mutex_);
            pool_.push_back(std::unique_ptr<StereoBlock>(b));
        };

        return BufferPtr(buffer.release(), deleter);
    }

    size_t buffer_size() const { return buffer_size_; }

private:
    size_t buffer_size_;
    std::vector<std::unique_ptr<StereoBlock>> pool_;
    std::mutex mutex_;
};

} // namespace audio

#endif // AUDIO_BUFFER_POOL_HPP
