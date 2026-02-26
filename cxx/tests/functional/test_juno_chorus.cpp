#include <gtest/gtest.h>
#include "fx/JunoChorus.hpp"
#include "AudioBuffer.hpp"
#include <vector>

TEST(JunoChorusTest, StereoSeparation) {
    const int sample_rate = 44100;
    audio::JunoChorus chorus(sample_rate);
    chorus.set_mode(audio::JunoChorus::Mode::I);

    const size_t frames = 512;
    std::vector<float> left(frames, 0.5f);
    std::vector<float> right(frames, 0.5f);
    
    audio::AudioBuffer buffer;
    buffer.left = left;
    buffer.right = right;

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
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
