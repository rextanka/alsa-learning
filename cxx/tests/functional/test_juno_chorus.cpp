#include <gtest/gtest.h>
#include "../TestHelper.hpp"
#include <vector>
#include <cmath>

/**
 * @file test_juno_chorus.cpp
 * @brief Functional verification of Juno Chorus stereo separation via Bridge API.
 */

TEST(JunoChorusTest, StereoSeparation) {
    test::init_test_environment();
    int sample_rate = test::get_safe_sample_rate(0);
    
    PRINT_TEST_HEADER(
        "Juno Chorus Stereo Separation",
        "Verifies that Juno Chorus creates stereo separation from mono input via Bridge.",
        "Mono VCO -> JunoChorus (Mode I) -> Bridge -> Output",
        "Significant difference between Left and Right channels after processing.",
        sample_rate
    );

    test::EngineWrapper engine(sample_rate);
    
    // Setup mono source
    set_param(engine.get(), "sine_gain", 1.0f);
    engine_note_on(engine.get(), 60, 0.8f);

    // Enable Chorus via Bridge
    engine_set_chorus_enabled(engine.get(), 1);
    engine_set_chorus_mode(engine.get(), 1); // Mode I

    const size_t frames = 512;
    std::vector<float> output(frames * 2);
    
    // Process a few blocks to allow LFO to move
    for(int i=0; i<10; ++i) {
        engine_process(engine.get(), output.data(), frames);
    }

    // After processing, left and right should be different
    bool different = false;
    for (size_t i = 0; i < frames; ++i) {
        if (std::abs(output[i*2] - output[i*2 + 1]) > 0.0001f) {
            different = true;
            break;
        }
    }
    EXPECT_TRUE(different);
    
    std::cout << "[JunoChorus] Stereo separation confirmed via Bridge: " << (different ? "YES" : "NO") << std::endl;
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
