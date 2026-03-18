/**
 * @file PatchAnalysis.hpp
 * @brief Spectral analysis helpers for patch functional tests.
 *
 * Re-exports audio::spectral_centroid() from src/dsp/analysis/SpectralAnalysis.hpp
 * so test files can include a single header without spelling out the source path.
 */

#ifndef PATCH_ANALYSIS_HPP
#define PATCH_ANALYSIS_HPP

#include "../../src/dsp/analysis/SpectralAnalysis.hpp"

// Hoist into the global namespace for test convenience.
using audio::spectral_centroid;

#endif // PATCH_ANALYSIS_HPP
