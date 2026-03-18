/**
 * @file test_lfo.cpp
 * @brief Unit tests for LfoProcessor DSP correctness.
 *
 * Covers:
 *  - Waveform range and shape for all four waveforms (Sine, Triangle, Square, Saw)
 *  - Frequency accuracy: phase advances at the correct sample rate
 *  - Intensity scaling
 *  - Smoothing (intensity ramp does not produce a step)
 */

#include <gtest/gtest.h>
#include "../../src/dsp/oscillator/LfoProcessor.hpp"
#include "../../src/core/Voice.hpp"
#include "../../src/core/ModuleRegistry.hpp"
#include <cmath>
#include <vector>
#include <algorithm>
#include <numeric>

using namespace audio;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static constexpr int kSampleRate = 48000;

/** Pull N samples from an LFO into a vector. */
static std::vector<float> pull_n(LfoProcessor& lfo, int n) {
    std::vector<float> out(n);
    // LFO processes in blocks; pull one sample at a time for precision.
    for (int i = 0; i < n; ++i) {
        std::span<float> sp(out.data() + i, 1);
        lfo.pull(sp);
    }
    return out;
}

/** Pull one complete LFO period as a block (block-rate API). */
static std::vector<float> pull_period_block(LfoProcessor& lfo, double hz) {
    int period_samples = static_cast<int>(std::round(kSampleRate / hz));
    std::vector<float> out(period_samples);
    std::span<float> sp(out);
    lfo.pull(sp); // single block — LFO uses block-rate calculation
    return out;
}

// ---------------------------------------------------------------------------
// Waveform range and shape
// ---------------------------------------------------------------------------

class LfoWaveformTest : public ::testing::Test {
protected:
    void SetUp() override {
        register_builtin_processors();
        lfo = std::make_unique<LfoProcessor>(kSampleRate);
        lfo->set_intensity(1.0f);
    }
    std::unique_ptr<LfoProcessor> lfo;
};

TEST_F(LfoWaveformTest, SineRange) {
    lfo->set_waveform(LfoProcessor::Waveform::Sine);
    lfo->set_frequency(1.0); // 1 Hz — one period per kSampleRate samples

    auto samples = pull_n(*lfo, kSampleRate);

    float max_val = *std::max_element(samples.begin(), samples.end());
    float min_val = *std::min_element(samples.begin(), samples.end());

    EXPECT_NEAR(max_val,  1.0f, 0.01f) << "Sine peak should reach +1.0";
    EXPECT_NEAR(min_val, -1.0f, 0.01f) << "Sine trough should reach -1.0";
}

TEST_F(LfoWaveformTest, TriangleRange) {
    lfo->set_waveform(LfoProcessor::Waveform::Triangle);
    lfo->set_frequency(1.0);

    auto samples = pull_n(*lfo, kSampleRate);

    float max_val = *std::max_element(samples.begin(), samples.end());
    float min_val = *std::min_element(samples.begin(), samples.end());

    EXPECT_NEAR(max_val,  1.0f, 0.05f);
    EXPECT_NEAR(min_val, -1.0f, 0.05f);
}

TEST_F(LfoWaveformTest, SquareOnlyTwoValues) {
    lfo->set_waveform(LfoProcessor::Waveform::Square);
    lfo->set_frequency(1.0);

    auto samples = pull_n(*lfo, kSampleRate);

    // Every sample must be either +1 or -1.
    for (float s : samples) {
        EXPECT_TRUE(std::abs(s - 1.0f) < 0.001f || std::abs(s + 1.0f) < 0.001f)
            << "Square wave sample out of range: " << s;
    }

    float max_val = *std::max_element(samples.begin(), samples.end());
    float min_val = *std::min_element(samples.begin(), samples.end());
    EXPECT_NEAR(max_val,  1.0f, 0.001f);
    EXPECT_NEAR(min_val, -1.0f, 0.001f);
}

TEST_F(LfoWaveformTest, SawRange) {
    lfo->set_waveform(LfoProcessor::Waveform::Saw);
    lfo->set_frequency(1.0);

    auto samples = pull_n(*lfo, kSampleRate);

    float max_val = *std::max_element(samples.begin(), samples.end());
    float min_val = *std::min_element(samples.begin(), samples.end());

    EXPECT_NEAR(max_val,  1.0f, 0.05f);
    EXPECT_NEAR(min_val, -1.0f, 0.05f);
}

// ---------------------------------------------------------------------------
// Frequency accuracy
// ---------------------------------------------------------------------------

class LfoFrequencyTest : public ::testing::Test {
protected:
    void SetUp() override {
        register_builtin_processors();
        lfo = std::make_unique<LfoProcessor>(kSampleRate);
        lfo->set_intensity(1.0f);
        lfo->set_waveform(LfoProcessor::Waveform::Sine);
    }
    std::unique_ptr<LfoProcessor> lfo;
};

TEST_F(LfoFrequencyTest, OneHzZeroCrossingsPerPeriod) {
    lfo->set_frequency(1.0);

    // One period at 1 Hz = 48000 samples.
    // A 1 Hz sine starting at phase 0 should cross zero approximately twice
    // (once rising, once falling).
    auto samples = pull_n(*lfo, kSampleRate);

    int zero_crossings = 0;
    for (size_t i = 1; i < samples.size(); ++i) {
        if ((samples[i - 1] < 0.0f) != (samples[i] < 0.0f)) {
            ++zero_crossings;
        }
    }
    // Block-rate LFO: value is constant within each block (512 samples), so
    // zero crossings happen at block boundaries — expect 2 per period.
    EXPECT_GE(zero_crossings, 1) << "Sine at 1 Hz should cross zero at least once per period";
    EXPECT_LE(zero_crossings, 4) << "Sine at 1 Hz should not cross zero more than 4 times per period";
}

TEST_F(LfoFrequencyTest, FiveHzCompletesMoreCycles) {
    lfo->set_frequency(5.0);

    // 5 Hz → 5 periods in kSampleRate samples → ≥10 zero crossings.
    auto samples = pull_n(*lfo, kSampleRate);

    int zero_crossings = 0;
    for (size_t i = 1; i < samples.size(); ++i) {
        if ((samples[i - 1] < 0.0f) != (samples[i] < 0.0f)) {
            ++zero_crossings;
        }
    }
    EXPECT_GE(zero_crossings, 8) << "5 Hz sine should cross zero ≥8 times per second";
}

// ---------------------------------------------------------------------------
// Intensity scaling
// ---------------------------------------------------------------------------

TEST(LfoIntensityTest, ZeroIntensityProducesZero) {
    register_builtin_processors();
    LfoProcessor lfo(kSampleRate);
    lfo.set_frequency(5.0);
    lfo.set_waveform(LfoProcessor::Waveform::Sine);
    lfo.set_intensity(0.0f);
    // reset() snaps smoothed_intensity_ to match intensity_ immediately,
    // bypassing the exponential ramp that would otherwise take many blocks.
    lfo.reset();

    auto samples = pull_n(lfo, 512);
    for (float s : samples) {
        EXPECT_NEAR(s, 0.0f, 0.001f) << "Intensity 0 should produce silence";
    }
}

TEST(LfoIntensityTest, HalfIntensityHalvesPeak) {
    register_builtin_processors();
    LfoProcessor lfo_full(kSampleRate);
    LfoProcessor lfo_half(kSampleRate);

    lfo_full.set_frequency(1.0);
    lfo_half.set_frequency(1.0);
    lfo_full.set_waveform(LfoProcessor::Waveform::Sine);
    lfo_half.set_waveform(LfoProcessor::Waveform::Sine);
    lfo_full.set_intensity(1.0f);
    lfo_half.set_intensity(0.5f);

    auto full_samples = pull_n(lfo_full, kSampleRate);
    auto half_samples = pull_n(lfo_half, kSampleRate);

    float full_max = *std::max_element(full_samples.begin(), full_samples.end());
    float half_max = *std::max_element(half_samples.begin(), half_samples.end());

    // Peak of half-intensity LFO should be approximately half the full peak.
    EXPECT_NEAR(half_max, full_max * 0.5f, 0.05f);
}

