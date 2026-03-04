/**
 * @file stereo_poly_test.cpp
 * @brief Functional test for "Mono-until-Mixer" architecture and Stereo Polyphonic spread.
 */

#include "../TestHelper.hpp"
#include <gtest/gtest.h>
#include <cmath>
#include <iostream>
#include <vector>

class StereoPolyTest : public ::testing::Test {
protected:
    void SetUp() override {
        sample_rate = test::get_safe_sample_rate(0);
        
        PRINT_TEST_HEADER(
            "Stereo Polyphonic Integrity",
            "Verifies mono-until-mixer architecture and stereo voice spreading.",
            "Poly Voices -> Panning -> Bridge -> Output",
            "Identical signals for mono (spread=0), distinct L/R for spread=1.0.",
            sample_rate
        );

        engine_wrapper = std::make_unique<test::EngineWrapper>(sample_rate);
    }

    int sample_rate;
    std::unique_ptr<test::EngineWrapper> engine_wrapper;
};

TEST_F(StereoPolyTest, MonoIntegrityTest) {
    EngineHandle engine = engine_wrapper->get();
    
    // Manual setup via Bridge since we don't have voice_spread in CInterface yet
    // Assuming spread defaults to 0 or we use standard note_on
    engine_note_on(engine, 60, 0.8f);

    const size_t frames = 512;
    std::vector<float> output(frames * 2);
    engine_process(engine, output.data(), frames);

    float total_diff = 0.0f;
    float peak = 0.0f;
    for (size_t i = 0; i < frames; ++i) {
        float left = output[i*2];
        float right = output[i*2 + 1];
        total_diff += std::abs(left - right);
        peak = std::max(peak, std::abs(left));
    }

    EXPECT_GT(peak, 0.001f);
    EXPECT_NEAR(total_diff, 0.0f, 1e-4f);
    
    std::cout << "[MonoIntegrity] Peak: " << peak << ", Diff: " << total_diff << std::endl;
}

TEST_F(StereoPolyTest, PannedNoteTest) {
    EngineHandle engine = engine_wrapper->get();
    
    // Use engine_set_note_pan to verify stereo logic
    engine_note_on(engine, 60, 0.8f);
    engine_set_note_pan(engine, 60, -1.0f); // Hard Left

    const size_t frames = 512;
    std::vector<float> output(frames * 2);
    
    // Process a few blocks to allow for any ramps
    for(int i=0; i<5; ++i) engine_process(engine, output.data(), frames);

    float peak_l = 0.0f;
    float peak_r = 0.0f;
    for (size_t i = 0; i < frames; ++i) {
        peak_l = std::max(peak_l, std::abs(output[i*2]));
        peak_r = std::max(peak_r, std::abs(output[i*2+1]));
    }
    
    EXPECT_GT(peak_l, 0.001f);
    EXPECT_LT(peak_r, 0.0001f);
    
    std::cout << "[PannedNote] Hard Left Peak L: " << peak_l << ", Peak R: " << peak_r << std::endl;
}
