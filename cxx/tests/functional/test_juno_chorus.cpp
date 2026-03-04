#include <gtest/gtest.h>
#include "../TestHelper.hpp"
#include "../../src/dsp/fx/JunoChorus.hpp"
#include <vector>

/**
 * @file test_juno_chorus.cpp
 * @brief Functional verification of Juno Chorus stereo separation.
 */

TEST(JunoChorusTest, StereoSeparation) {
    int sample_rate = test::get_safe_sample_rate(0);
    
    PRINT_TEST_HEADER(
        "Juno Chorus Stereo Separation",
        "Verifies that Juno Chorus creates stereo separation from mono input.",
        "Mono Input -> JunoChorus (Mode I) -> Stereo Output",
        "Significant difference between Left and Right channels after processing.",
        sample_rate
    );

    audio::JunoChorus chorus(static_cast<double>(sample_rate));
    chorus.set_mode(audio::JunoChorus::Mode::I);

    const size_t frames = 512;
    std::vector<float> left(frames, 0.5f);
    std::vector<float> right(frames, 0.5f);
    
    audio::AudioBuffer buffer(left, right);
    chorus.pull(buffer);

    // After processing, left and right should be different due to inverted LFOs
    bool different = false;
    for (size_t i = 0; i < frames; ++i) {
        if (std::abs(buffer.left[i] - buffer.right[i]) > 0.0001f) {
            different = true;
            break;
        }
    }
    EXPECT_TRUE(different);
    
    std::cout << "[JunoChorus] Stereo separation confirmed: " << (different ? "YES" : "NO") << std::endl;
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
