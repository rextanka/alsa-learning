/**
 * @file stereo_poly_test.cpp
 * @brief Functional test for "Mono-until-Mixer" architecture and Stereo Polyphonic spread.
 */

#include "../../src/core/VoiceManager.hpp"
#include "../../src/core/AudioBuffer.hpp"
#include "../../src/core/AudioSettings.hpp"
#include <gtest/gtest.h>
#include <cmath>
#include <iostream>
#include <vector>

using namespace audio;

class StereoPolyTest : public ::testing::Test {
protected:
    void SetUp() override {
        sample_rate = 48000;
        frames_per_block = 512;
        manager = std::make_unique<VoiceManager>(sample_rate);
    }

    int sample_rate;
    int frames_per_block;
    std::unique_ptr<VoiceManager> manager;
};

TEST_F(StereoPolyTest, MonoIntegrityTest) {
    // When spread is 0, output should be identical on both channels
    manager->set_voice_spread(0.0f);
    manager->note_on(60, 0.8f); // Middle C

    std::vector<float> left_vec(frames_per_block);
    std::vector<float> right_vec(frames_per_block);
    AudioBuffer output{left_vec, right_vec};
    manager->pull(output);

    // Verify signals are non-zero and identical
    float total_diff = 0.0f;
    float peak = 0.0f;
    for (int i = 0; i < frames_per_block; ++i) {
        total_diff += std::abs(output.left[i] - output.right[i]);
        peak = std::max(peak, std::abs(output.left[i]));
    }

    EXPECT_GT(peak, 0.001f);
    EXPECT_NEAR(total_diff, 0.0f, 1e-5f);
    
    std::cout << "[MonoIntegrity] Peak: " << peak << ", Diff: " << total_diff << std::endl;
}

TEST_F(StereoPolyTest, StereoSpreadTest) {
    // When spread is 1.0, voices should occupy different positions
    manager->set_voice_spread(1.0f);
    
    // Voice 0 should be panned Left (-1.0)
    manager->note_on(60, 0.8f); 
    
    std::vector<float> l0(frames_per_block);
    std::vector<float> r0(frames_per_block);
    AudioBuffer v0_output{l0, r0};
    manager->pull(v0_output);
    
    float v0_peak_l = 0.0f;
    float v0_peak_r = 0.0f;
    for (int i = 0; i < frames_per_block; ++i) {
        v0_peak_l = std::max(v0_peak_l, std::abs(v0_output.left[i]));
        v0_peak_r = std::max(v0_peak_r, std::abs(v0_output.right[i]));
    }
    
    EXPECT_GT(v0_peak_l, 0.001f);
    EXPECT_NEAR(v0_peak_r, 0.0f, 1e-5f);
    
    manager->note_off(60);
    manager->reset();

    // Voice 1 should be panned Right (1.0)
    manager->note_on(60, 0.8f); // Voice 0
    manager->note_on(64, 0.8f); // Voice 1
    
    std::vector<float> lp(frames_per_block);
    std::vector<float> rp(frames_per_block);
    AudioBuffer poly_output{lp, rp};
    manager->pull(poly_output);
    
    manager->note_off(60);
    for(int i=0; i<10; ++i) manager->pull(poly_output);
    
    float v1_peak_l = 0.0f;
    float v1_peak_r = 0.0f;
    for (int i = 0; i < frames_per_block; ++i) {
        v1_peak_l = std::max(v1_peak_l, std::abs(poly_output.left[i]));
        v1_peak_r = std::max(v1_peak_r, std::abs(poly_output.right[i]));
    }
    
    EXPECT_GT(v1_peak_r, 0.001f);
    EXPECT_LT(v1_peak_l, v1_peak_r * 0.1f);
    
    std::cout << "[StereoSpread] V0(L): " << v0_peak_l << ", V0(R): " << v0_peak_r << std::endl;
    std::cout << "[StereoSpread] V1(L): " << v1_peak_l << ", V1(R): " << v1_peak_r << std::endl;
}

TEST_F(StereoPolyTest, ConstantPowerLawTest) {
    manager->set_voice_spread(0.0f); // Center
    manager->note_on(60, 0.8f);
    
    std::vector<float> lc(frames_per_block);
    std::vector<float> rc(frames_per_block);
    AudioBuffer center_out{lc, rc};
    manager->pull(center_out);
    float center_peak = 0.0f;
    for(int i=0; i<frames_per_block; ++i) center_peak = std::max(center_peak, center_out.left[i]);
    
    manager->reset();
    
    manager->note_on_panned(60, 0.8f, -1.0f);
    std::vector<float> ll(frames_per_block);
    std::vector<float> rl(frames_per_block);
    AudioBuffer left_out{ll, rl};
    manager->pull(left_out);
    float left_peak = 0.0f;
    for(int i=0; i<frames_per_block; ++i) left_peak = std::max(left_peak, left_out.left[i]);

    EXPECT_NEAR(left_peak, center_peak * 1.414f, 0.05f);
    
    std::cout << "[ConstantPower] Center: " << center_peak << ", Left: " << left_peak << " (Ratio: " << left_peak/center_peak << ")" << std::endl;
}
