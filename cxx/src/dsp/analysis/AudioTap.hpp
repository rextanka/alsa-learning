/**
 * @file AudioTap.hpp
 * @brief RT-Safe Audio Tap Processor for non-intrusive analysis.
 */

#ifndef AUDIO_TAP_HPP
#define AUDIO_TAP_HPP

#include "../Processor.hpp"
#include <atomic>
#include <vector>
#include <algorithm>

namespace audio {

/**
 * @brief RT-Safe Audio Tap Processor.
 * 
 * A "Tee" junction in the modular graph. It pulls audio from its source 
 * and passes it to the next block while simultaneously copying the buffer 
 * into an internal lock-free ring buffer for analysis.
 */
class AudioTap : public Processor {
public:
    /**
     * @brief Construct an AudioTap with a power-of-two buffer size.
     * @param buffer_size Must be a power of two (default 8192).
     */
    explicit AudioTap(size_t buffer_size = 8192) {
        // Enforce Power-of-Two Requirement (ARCH_PLAN.md)
        if ((buffer_size & (buffer_size - 1)) != 0 || buffer_size == 0) {
            // Find next power of two if not already one
            size_t p = 1;
            while (p < buffer_size) p <<= 1;
            buffer_size_ = p;
        } else {
            buffer_size_ = buffer_size;
        }
        
        mask_ = buffer_size_ - 1;
        buffer_.resize(buffer_size_, 0.0f);
        write_head_.store(0, std::memory_order_relaxed);
    }

    /**
     * @brief Reset the circular buffer and write head.
     */
    void reset() override {
        write_head_.store(0, std::memory_order_relaxed);
        std::fill(buffer_.begin(), buffer_.end(), 0.0f);
    }

    /**
     * @brief Pull audio from the tap for the analysis (consumer) thread.
     * 
     * Copies the most recent 'target.size()' samples from the circular buffer.
     * 
     * @param target Buffer to fill with tapped data.
     * @return size_t Number of samples copied.
     */
    size_t read(std::span<float> target) const {
        size_t current_write = write_head_.load(std::memory_order_acquire);
        size_t to_copy = std::min(target.size(), buffer_size_);
        
        // Calculate start index to get the most recent samples
        size_t start_idx = (current_write + buffer_size_ - to_copy) & mask_;
        
        for (size_t i = 0; i < to_copy; ++i) {
            target[i] = buffer_[(start_idx + i) & mask_];
        }
        
        return to_copy;
    }

    /**
     * @brief Check how many samples are available in the buffer.
     * Useful for the test to know if enough data has been collected.
     */
    size_t get_capacity() const { return buffer_size_; }

protected:
    /**
     * @brief RT-Safe implementation of the pull protocol.
     * 
     * Pulls from input, then copies the resulting span into the ring buffer.
     */
    void do_pull(std::span<float> output, const VoiceContext* voice_context = nullptr) override {
        // 1. Pull from input source if connected
        if (!inputs_.empty() && inputs_[0] != nullptr) {
            inputs_[0]->pull(output, voice_context);
        } else {
            // If no input, just fill with silence (to be safe)
            std::fill(output.begin(), output.end(), 0.0f);
        }

        // 2. Copy the pulled audio into our internal analysis ring buffer
        size_t head = write_head_.load(std::memory_order_relaxed);
        for (float sample : output) {
            buffer_[head] = sample;
            head = (head + 1) & mask_;
        }
        
        // Commit the write head so the analysis thread can see it
        write_head_.store(head, std::memory_order_release);
    }

private:
    std::vector<float> buffer_;
    size_t buffer_size_;
    size_t mask_;
    std::atomic<size_t> write_head_{0};
};

} // namespace audio

#endif // AUDIO_TAP_HPP
