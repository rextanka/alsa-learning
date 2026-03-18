/**
 * @file SpectralAnalysis.hpp
 * @brief Spectral analysis utilities built on DctProcessor.
 *
 * Provides frequency-domain analysis helpers for both production use (e.g.
 * automated quality checks, patch verification) and test code.
 *
 * DCT-II bin k maps to frequency:
 *   f_k = k * sample_rate / (2 * N)
 */

#ifndef SPECTRAL_ANALYSIS_HPP
#define SPECTRAL_ANALYSIS_HPP

#include "DctProcessor.hpp"
#include <vector>
#include <cmath>

namespace audio {

/**
 * @brief Compute the DCT-based spectral centroid of a mono audio window.
 *
 * Uses a DCT-II transform. Bin k maps to f_k = k * sr / (2 * N).
 * Returns the frequency-weighted mean of magnitude bins 1..N/2 (Hz).
 * Returns 0 if the window is silent.
 *
 * @param window  Mono audio samples (any size; power-of-two recommended).
 * @param sample_rate  Sample rate in Hz.
 */
inline float spectral_centroid(const std::vector<float>& window, int sample_rate) {
    const size_t N = window.size();
    DctProcessor dct(N, N);
    std::vector<float> mags(N);
    dct.process(window, mags);
    double num = 0.0, den = 0.0;
    for (size_t k = 1; k < N / 2; ++k) {
        double f = double(k) * double(sample_rate) / (2.0 * double(N));
        num += f * double(mags[k]);
        den += double(mags[k]);
    }
    return den > 1e-9 ? float(num / den) : 0.0f;
}

} // namespace audio

#endif // SPECTRAL_ANALYSIS_HPP
