/**
 * @file test_distortion_processor.cpp
 * @brief Unit tests for DistortionProcessor — verifies drive and character
 *        controls produce measurable changes in the output signal.
 */

#include <gtest/gtest.h>
#include "fx/DistortionProcessor.hpp"
#include <vector>
#include <cmath>
#include <numeric>

using namespace audio;

static constexpr int    kSampleRate = 48000;
static constexpr size_t kBlockSize  = 4096; // > ramp_samples (480) so params settle

static void fill_sine(std::vector<float>& buf, float freq, float amp) {
    for (size_t i = 0; i < buf.size(); ++i)
        buf[i] = amp * std::sinf(2.0f * static_cast<float>(M_PI)
                                 * freq * static_cast<float>(i)
                                 / static_cast<float>(kSampleRate));
}

static float peak_pos(const std::vector<float>& buf) {
    float p = 0.0f;
    for (float s : buf) p = std::max(p, s);
    return p;
}

static float peak_neg(const std::vector<float>& buf) {
    float p = 0.0f;
    for (float s : buf) p = std::min(p, s);
    return p;
}

static float rms(const std::vector<float>& buf) {
    float sum = 0.0f;
    for (float s : buf) sum += s * s;
    return std::sqrtf(sum / static_cast<float>(buf.size()));
}

// ---------------------------------------------------------------------------
// Drive tests
// ---------------------------------------------------------------------------

TEST(DistortionProcessor, PassesSignalAtDriveOne) {
    DistortionProcessor dist(kSampleRate);
    dist.apply_parameter("drive",     1.0f);
    dist.apply_parameter("character", 0.0f);

    std::vector<float> buf(kBlockSize);
    fill_sine(buf, 440.0f, 0.5f);
    dist.pull(std::span<float>(buf));

    // At drive=1 the waveshaper input peak is 0.5; tanh(0.5) ≈ 0.46 — signal present
    // and only lightly compressed. Peak must be well above zero.
    EXPECT_GT(peak_pos(buf), 0.3f);
    EXPECT_LT(peak_pos(buf), 0.55f); // not heavily clipped
}

TEST(DistortionProcessor, HighDriveClipsToNearUnity) {
    DistortionProcessor dist(kSampleRate);
    dist.apply_parameter("drive",     20.0f);
    dist.apply_parameter("character", 0.0f);

    std::vector<float> buf(kBlockSize);
    fill_sine(buf, 440.0f, 0.5f);
    dist.pull(std::span<float>(buf));

    // Waveshaper input peak = 0.5 × 20 = 10; tanh(10) ≈ 1.0 → saturated
    EXPECT_GT(peak_pos(buf), 0.85f);
}

TEST(DistortionProcessor, HighDriveRaisesRMSVsLowDrive) {
    auto run = [&](float drive_val) {
        DistortionProcessor dist(kSampleRate);
        dist.apply_parameter("drive",     drive_val);
        dist.apply_parameter("character", 0.0f);
        std::vector<float> buf(kBlockSize);
        fill_sine(buf, 440.0f, 0.5f);
        dist.pull(std::span<float>(buf));
        return rms(buf);
    };

    // Heavy clipping squashes peaks toward a square wave, raising RMS for the
    // same input level: rms(drive=20) must be clearly above rms(drive=1).
    EXPECT_GT(run(20.0f), run(1.0f) * 1.5f);
}

// ---------------------------------------------------------------------------
// Character tests
// ---------------------------------------------------------------------------

TEST(DistortionProcessor, CharacterZeroIsSymmetric) {
    DistortionProcessor dist(kSampleRate);
    dist.apply_parameter("drive",     10.0f);
    dist.apply_parameter("character", 0.0f);

    std::vector<float> buf(kBlockSize);
    fill_sine(buf, 440.0f, 0.3f);
    dist.pull(std::span<float>(buf));

    // tanh is an odd function — positive and negative peaks must be equal
    float pos =  peak_pos(buf);
    float neg = -peak_neg(buf);
    EXPECT_NEAR(pos, neg, neg * 0.05f); // within 5%
}

TEST(DistortionProcessor, CharacterOneIsAsymmetric) {
    DistortionProcessor dist(kSampleRate);
    dist.apply_parameter("drive",     10.0f);
    dist.apply_parameter("character", 1.0f);

    std::vector<float> buf(kBlockSize);
    fill_sine(buf, 440.0f, 0.3f);
    dist.pull(std::span<float>(buf));

    // Hard path: positive → tanh(x·4)·0.5 (peaks near +0.5),
    //            negative → tanh(x)        (peaks near -1.0).
    // Negative magnitude must be at least 1.5× the positive.
    float pos =  peak_pos(buf);
    float neg = -peak_neg(buf);
    EXPECT_GT(neg, pos * 1.5f);
}

TEST(DistortionProcessor, CharacterChangeProducesAudibleDifference) {
    auto run = [&](float character_val) {
        DistortionProcessor dist(kSampleRate);
        dist.apply_parameter("drive",     10.0f);
        dist.apply_parameter("character", character_val);
        std::vector<float> buf(kBlockSize);
        fill_sine(buf, 440.0f, 0.3f);
        dist.pull(std::span<float>(buf));
        return buf;
    };

    auto buf0 = run(0.0f);
    auto buf1 = run(1.0f);

    // Compute RMS of the difference — should be substantial
    float diff = 0.0f;
    for (size_t i = 0; i < kBlockSize; ++i) {
        float d = buf0[i] - buf1[i];
        diff += d * d;
    }
    diff = std::sqrtf(diff / static_cast<float>(kBlockSize));
    EXPECT_GT(diff, 0.1f);
}
