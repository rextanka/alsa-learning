/**
 * @file DctProcessor.hpp
 * @brief Self-contained DCT-II implementation with windowing.
 */

#ifndef DCT_PROCESSOR_HPP
#define DCT_PROCESSOR_HPP

#include <vector>
#include <cmath>
#include <numbers>
#include <span>

namespace audio {

/**
 * @brief Discrete Cosine Transform (DCT-II) Processor.
 * 
 * Transforms time-domain audio buffers into frequency-domain coefficients.
 * Includes a pre-computed cosine LUT and Hann windowing for accuracy.
 */
class DctProcessor {
public:
    /**
     * @brief Construct a DCT processor with optional zero-padding.
     * @param input_size The number of actual audio samples to window.
     * @param dct_size The size of the DCT transform (must be >= input_size, power of two recommended).
     */
    explicit DctProcessor(size_t input_size, size_t dct_size) 
        : input_size_(input_size), dct_size_(dct_size) {
        
        // 1. Precompute Hann window for the input buffer
        hann_window_.resize(input_size_);
        if (input_size_ > 1) {
            for (size_t n = 0; n < input_size_; ++n) {
                hann_window_[n] = static_cast<float>(0.5 * (1.0 - std::cos(2.0 * std::numbers::pi * n / (input_size_ - 1))));
            }
        } else {
            hann_window_[0] = 1.0f;
        }

        // 2. Precompute cosine LUT for DCT-II of size 'dct_size'
        // Formula: X[k] = sum_{n=0}^{N-1} x[n] * cos( (pi/N) * (n + 0.5) * k )
        lut_.resize(dct_size_ * dct_size_);
        const double pi_over_n = std::numbers::pi / static_cast<double>(dct_size_);
        for (size_t k = 0; k < dct_size_; ++k) {
            double k_double = static_cast<double>(k);
            for (size_t n = 0; n < dct_size_; ++n) {
                lut_[k * dct_size_ + n] = static_cast<float>(std::cos(pi_over_n * (static_cast<double>(n) + 0.5) * k_double));
            }
        }
    }

    /**
     * @brief Legacy constructor for backward compatibility (no padding).
     */
    explicit DctProcessor(size_t size) : DctProcessor(size, size) {}

    /**
     * @brief Compute the DCT-II of the input buffer with zero-padding.
     * 
     * @param input Time-domain input samples (must be at least input_size_).
     * @param output Frequency-domain coefficients (must be at least dct_size_).
     */
    void process(std::span<const float> input, std::span<float> output) {
        if (input.size() < input_size_ || output.size() < dct_size_) {
            return;
        }

        // 1. Create a zero-padded buffer of size dct_size_
        std::vector<float> padded(dct_size_, 0.0f);
        
        // 2. Apply windowing to the audio samples and copy to padded buffer
        for (size_t n = 0; n < input_size_; ++n) {
            padded[n] = input[n] * hann_window_[n];
        }

        // 3. Perform O(N^2) DCT-II on the full padded buffer
        for (size_t k = 0; k < dct_size_; ++k) {
            float sum = 0.0f;
            const size_t k_offset = k * dct_size_;
            for (size_t n = 0; n < dct_size_; ++n) {
                sum += padded[n] * lut_[k_offset + n];
            }
            // We store the absolute magnitude for peak detection
            output[k] = std::abs(sum);
        }
    }

    size_t get_input_size() const { return input_size_; }
    size_t get_dct_size() const { return dct_size_; }
    
    // Legacy support
    size_t get_size() const { return dct_size_; }

private:
    size_t input_size_;
    size_t dct_size_;
    std::vector<float> lut_;
    std::vector<float> hann_window_;
};

} // namespace audio

#endif // DCT_PROCESSOR_HPP
