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

namespace audio {

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
            pool_.push_back(std::make_unique<std::vector<float>>(buffer_size_));
        }
    }

    /**
     * @brief Borrow a buffer from the pool.
     * 
     * @return std::unique_ptr<std::vector<float>, std::function<void(std::vector<float>*)>> 
     *         A smart pointer that returns the buffer to the pool when it goes out of scope.
     */
    using BufferPtr = std::unique_ptr<std::vector<float>, std::function<void(std::vector<float>*)>>;

    BufferPtr borrow() {
        std::lock_guard<std::mutex> lock(mutex_);
        
        std::unique_ptr<std::vector<float>> buffer;
        if (pool_.empty()) {
            buffer = std::make_unique<std::vector<float>>(buffer_size_);
        } else {
            buffer = std::move(pool_.back());
            pool_.pop_back();
        }

        // Custom deleter returns the buffer to the pool
        auto deleter = [this](std::vector<float>* b) {
            std::lock_guard<std::mutex> lock(mutex_);
            pool_.push_back(std::unique_ptr<std::vector<float>>(b));
        };

        return BufferPtr(buffer.release(), deleter);
    }

    size_t buffer_size() const { return buffer_size_; }

private:
    size_t buffer_size_;
    std::vector<std::unique_ptr<std::vector<float>>> pool_;
    std::mutex mutex_;
};

} // namespace audio

#endif // AUDIO_BUFFER_POOL_HPP
