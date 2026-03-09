/**
 * @file PitchDetector.hpp
 * @brief Peak-picking and parabolic interpolation for frequency estimation. 
 */

#ifndef PITCH_DETECTOR_HPP
#define PITCH_DETECTOR_HPP

#include <vector>
#include <span>
#include <algorithm>
#include <cmath>

namespace audio {

/**
 * @brief Pitch Detection Utility.
 * 
 * Analyzes spectral magnitudes (e.g., from DCT) to find the dominant frequency.
 * Uses parabolic interpolation to achieve sub-bin accuracy, which is critical
 * for verifying pitch within ±1Hz.
 */
class PitchDetector {
public:
    /**
     * @brief Detect the dominant frequency from spectral magnitudes.
     * 
     * @param magnitudes The magnitudes of the frequency bins.
     * @param sample_rate The sample rate used to collect the audio.
     * @return float The detected frequency in Hz.
     */
    static float detect(std::span<const float> magnitudes, float sample_rate) {
        if (magnitudes.size() < 3) {
            return 0.0f;
        }

        // 1. Find the peak bin (skipping bin 0 to avoid DC offset issues)
        // Search up to Nyquist (which is magnitudes.size() - 1 for DCT-II)
        auto it_start = magnitudes.begin() + 1;
        auto it_end = magnitudes.end();
        auto max_it = std::max_element(it_start, it_end);
        
        size_t bin = std::distance(magnitudes.begin(), max_it);
        float beta = *max_it;

        // If the peak is too low, treat it as silence
        if (beta < 1e-5f) {
            return 0.0f;
        }

        // 2. Apply Parabolic Interpolation for sub-bin accuracy
        // Using standard parabolic interpolation on magnitudes first
        float fractional_bin = static_cast<float>(bin);
        
        if (bin > 0 && bin < magnitudes.size() - 1) {
            float alpha = magnitudes[bin - 1];
            float gamma = magnitudes[bin + 1];
            
            float denominator = (alpha - 2.0f * beta + gamma);
            if (std::abs(denominator) > 1e-7f) {
                float p = 0.5f * (alpha - gamma) / denominator;
                // Constraints: p must be within [-0.5, 0.5]
                if (p > 0.5f) p = 0.5f;
                if (p < -0.5f) p = -0.5f;
                fractional_bin += p;
            }
        }

        // 3. Convert fractional bin to frequency
        // In DCT-II of size N, bin k represents frequency: f = (k * sample_rate) / (2 * N)
        float n = static_cast<float>(magnitudes.size());
        return (fractional_bin * sample_rate) / (2.0f * n);
    }
};

} // namespace audio

#endif // PITCH_DETECTOR_HPP
